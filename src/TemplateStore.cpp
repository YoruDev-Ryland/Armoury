#include "TemplateStore.h"
#include "GW2Api.h"
#include "ChatCode.h"
#include "Shared.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <set>
#include <windows.h>

using json = nlohmann::json;

// ── Internal state ────────────────────────────────────────────────────────────

namespace
{
    std::mutex g_Mx;

    std::vector<TemplateStore::StoredBuildTemplate>     g_Builds;
    std::vector<TemplateStore::StoredEquipmentTemplate> g_Equipment;

    // Global resolution caches (ID -> string)
    std::unordered_map<int, std::string> g_SpecNames;
    std::unordered_map<int, std::string> g_SkillNames;
    std::unordered_map<int, std::string> g_TraitNames;  // /v2/traits
    std::unordered_map<int, std::string> g_ItemNames;
    std::unordered_map<int, std::string> g_ItemRarities;

    // Chat-code caches
    // spec ID -> array of 9 major trait IDs ([tier*3+pos])
    std::unordered_map<int, std::array<int,9>> g_SpecMajorTraits;
    // profession name -> (palette ID -> API skill ID)
    std::unordered_map<std::string, std::unordered_map<int,int>> g_ProfPalette;

    // Background resolve thread
    std::thread      g_ResolveThread;
    std::atomic_bool g_ResolveStop{false};
    std::atomic_bool g_ResolvePending{false};
    std::string      g_ResolveApiKey;

    // Generate a simple unique ID
    std::string MakeId(const std::string& prefix)
    {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return prefix + "_" + std::to_string(now);
    }

    // ---------- JSON helpers --------------------------------------------------

    json SerialiseSkillBar(const GW2Api::SkillBar& b)
    {
        json j;
        j["heal"]  = b.heal;
        j["elite"] = b.elite;
        j["utilities"] = json::array({b.utilities[0], b.utilities[1], b.utilities[2]});
        j["legend1"] = b.legend1;
        j["legend2"] = b.legend2;
        j["terr_pet1"] = b.terrestrialPet1;
        j["terr_pet2"] = b.terrestrialPet2;
        j["aq_pet1"]   = b.aquaticPet1;
        j["aq_pet2"]   = b.aquaticPet2;
        return j;
    }

    GW2Api::SkillBar DeserialiseSkillBar(const json& j)
    {
        GW2Api::SkillBar b{};
        if (!j.is_object()) return b;
        b.heal  = j.value("heal",  0);
        b.elite = j.value("elite", 0);
        if (j.contains("utilities") && j["utilities"].is_array())
        {
            for (int i = 0; i < 3 && i < (int)j["utilities"].size(); ++i)
                b.utilities[i] = j["utilities"][i].is_number()
                                  ? j["utilities"][i].get<int>() : 0;
        }
        b.legend1 = j.value("legend1", 0);
        b.legend2 = j.value("legend2", 0);
        b.terrestrialPet1 = j.value("terr_pet1", 0);
        b.terrestrialPet2 = j.value("terr_pet2", 0);
        b.aquaticPet1     = j.value("aq_pet1",   0);
        b.aquaticPet2     = j.value("aq_pet2",   0);
        return b;
    }

    json SerialiseBuildTab(const GW2Api::BuildTab& t)
    {
        json j;
        j["tab"]        = t.tabNumber;
        j["is_active"]  = t.isActive;
        j["build_name"] = t.buildName;
        j["profession"] = t.profession;
        json specs = json::array();
        for (auto& s : t.specs)
        {
            json js;
            js["id"] = s.id;
            js["traits"] = json::array({s.traits[0], s.traits[1], s.traits[2]});
            specs.push_back(js);
        }
        j["specs"]          = specs;
        j["skills"]         = SerialiseSkillBar(t.skills);
        j["aquatic_skills"] = SerialiseSkillBar(t.aquaticSkills);
        return j;
    }

    GW2Api::BuildTab DeserialiseBuildTab(const json& j)
    {
        GW2Api::BuildTab t{};
        if (!j.is_object()) return t;
        t.tabNumber = j.value("tab", 0);
        t.isActive  = j.value("is_active", false);
        t.buildName = j.value("build_name", std::string{});
        t.profession = j.value("profession", std::string{});
        if (j.contains("specs") && j["specs"].is_array())
        {
            int idx = 0;
            for (auto& js : j["specs"])
            {
                if (idx >= 3) break;
                t.specs[idx].id = js.value("id", 0);
                if (js.contains("traits") && js["traits"].is_array())
                    for (int ti = 0; ti < 3 && ti < (int)js["traits"].size(); ++ti)
                        t.specs[idx].traits[ti] = js["traits"][ti].is_number()
                                                   ? js["traits"][ti].get<int>() : 0;
                ++idx;
            }
        }
        if (j.contains("skills"))         t.skills        = DeserialiseSkillBar(j["skills"]);
        if (j.contains("aquatic_skills")) t.aquaticSkills = DeserialiseSkillBar(j["aquatic_skills"]);
        return t;
    }

    json SerialiseEquipTab(const GW2Api::EquipmentTab& t)
    {
        json j;
        j["tab"]       = t.tabNumber;
        j["is_active"] = t.isActive;
        j["name"]      = t.tabName;
        json pieces = json::array();
        for (auto& p : t.pieces)
        {
            json jp;
            jp["item_id"]  = p.itemId;
            jp["slot"]     = p.slot;
            jp["skin_id"]  = p.skinId;
            jp["stats_id"] = p.statsId;
            jp["stats_name"] = p.statsName;
            jp["binding"]  = p.binding;
            jp["upgrades"] = p.upgradeIds;
            jp["infusions"] = p.infusionIds;
            pieces.push_back(jp);
        }
        j["pieces"] = pieces;
        return j;
    }

    GW2Api::EquipmentTab DeserialiseEquipTab(const json& j)
    {
        GW2Api::EquipmentTab t{};
        if (!j.is_object()) return t;
        t.tabNumber = j.value("tab", 0);
        t.isActive  = j.value("is_active", false);
        t.tabName   = j.value("name", std::string{});
        if (j.contains("pieces") && j["pieces"].is_array())
        {
            for (auto& jp : j["pieces"])
            {
                GW2Api::EquipmentPiece p;
                p.itemId    = jp.value("item_id",  0);
                p.slot      = jp.value("slot",     std::string{});
                p.skinId    = jp.value("skin_id",  0);
                p.statsId   = jp.value("stats_id", 0);
                p.statsName = jp.value("stats_name", std::string{});
                p.binding   = jp.value("binding",  std::string{});
                if (jp.contains("upgrades") && jp["upgrades"].is_array())
                    for (auto& u : jp["upgrades"]) if (u.is_number()) p.upgradeIds.push_back(u.get<int>());
                if (jp.contains("infusions") && jp["infusions"].is_array())
                    for (auto& u : jp["infusions"]) if (u.is_number()) p.infusionIds.push_back(u.get<int>());
                t.pieces.push_back(std::move(p));
            }
        }
        return t;
    }

    // ---------- Storage path -------------------------------------------------

    std::string StorePath()
    {
        if (!APIDefs || !APIDefs->Paths_GetAddonDirectory) return "";
        std::string dir = APIDefs->Paths_GetAddonDirectory("Armoury");
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir + "\\armoury.json";
    }

    // ---------- Background resolve worker ------------------------------------

    // Try to generate a chat code for a build template.
    // Must be called with g_Mx held.
    void TryGenerateChatCode(TemplateStore::StoredBuildTemplate& b)
    {
        if (!b.chatCode.empty()) return; // already have one

        // Check if we have palette skills (all zeros means unknown)
        bool hasPalette = false;
        for (auto p : b.paletteSkills) if (p) { hasPalette = true; break; }
        if (!hasPalette) return;

        // Check if we have all spec major traits to derive choices
        // (if all chosen traits are 0 we still generate but choices will be 0)
        uint8_t choices[3][3] = {};
        ChatCode::DeriveChoices(b.rawTab, g_SpecMajorTraits, choices);
        memcpy(b.specTraitChoices, choices, sizeof(choices));

        const uint8_t* pets    = (b.profession == "Ranger")   ? b.rangerPets      : nullptr;
        const uint8_t* legs    = (b.profession == "Revenant") ? b.revenantLegends  : nullptr;
        const uint16_t* inact  = (b.profession == "Revenant") ? b.revenantInactive : nullptr;

        int specIds[3] = {};
        for (int s = 0; s < 3; ++s) specIds[s] = b.rawTab.specs[s].id;

        b.chatCode = ChatCode::EncodeBuildCode(
            b.profession, specIds, choices, b.paletteSkills,
            pets, legs, inact);
    }

    void ResolveWorker(std::string apiKey)
    {
        // Collect all unresolved IDs
        std::set<int> specIds, skillIds, traitIds, itemIds, statsIds;

        {
            std::lock_guard<std::mutex> lk(g_Mx);
            for (auto& b : g_Builds)
            {
                for (auto& s : b.rawTab.specs)
                {
                    if (s.id && !g_SpecNames.count(s.id)) specIds.insert(s.id);
                    for (int t : s.traits)
                        if (t && !g_TraitNames.count(t)) traitIds.insert(t);
                }

                auto addSkill = [&](int id) {
                    if (id && !g_SkillNames.count(id)) skillIds.insert(id);
                };
                addSkill(b.rawTab.skills.heal);
                addSkill(b.rawTab.skills.elite);
                for (int u : b.rawTab.skills.utilities) addSkill(u);
            }
            for (auto& e : g_Equipment)
            {
                for (auto& p : e.rawTab.pieces)
                {
                    if (p.itemId && !g_ItemNames.count(p.itemId)) itemIds.insert(p.itemId);
                    for (int u : p.upgradeIds)   if (u && !g_ItemNames.count(u)) itemIds.insert(u);
                    for (int inf : p.infusionIds) if (inf && !g_ItemNames.count(inf)) itemIds.insert(inf);
                    if (p.statsId && p.statsName.empty()) statsIds.insert(p.statsId);
                }
            }
        }

        // Fetch specs
        if (!specIds.empty() && !g_ResolveStop)
        {
            auto infos = GW2Api::FetchSpecInfos(
                std::vector<int>(specIds.begin(), specIds.end()));
            std::lock_guard<std::mutex> lk(g_Mx);
            for (auto& si : infos)
            {
                g_SpecNames[si.id] = si.name;
                // Cache major traits (9 entries per spec)
                if (si.majorTraitIds.size() >= 9)
                {
                    std::array<int,9> arr{};
                    for (int i = 0; i < 9; ++i) arr[i] = si.majorTraitIds[i];
                    g_SpecMajorTraits[si.id] = arr;
                }
            }
        }

        // Fetch traits
        if (!traitIds.empty() && !g_ResolveStop)
        {
            auto infos = GW2Api::FetchTraitInfos(
                std::vector<int>(traitIds.begin(), traitIds.end()));
            std::lock_guard<std::mutex> lk(g_Mx);
            for (auto& ti : infos) g_TraitNames[ti.id] = ti.name;
        }

        // Fetch skills
        if (!skillIds.empty() && !g_ResolveStop)
        {
            auto infos = GW2Api::FetchSkillInfos(
                std::vector<int>(skillIds.begin(), skillIds.end()));
            std::lock_guard<std::mutex> lk(g_Mx);
            for (auto& si : infos) g_SkillNames[si.id] = si.name;
        }

        // Fetch items (upgraded/infusions included)
        if (!itemIds.empty() && !g_ResolveStop)
        {
            auto infos = GW2Api::FetchItemInfos(
                std::vector<int>(itemIds.begin(), itemIds.end()));
            std::lock_guard<std::mutex> lk(g_Mx);
            for (auto& ii : infos)
            {
                g_ItemNames[ii.id]    = ii.name;
                g_ItemRarities[ii.id] = ii.rarity;
            }
        }

        // Fetch stats that have no name yet
        if (!statsIds.empty() && !g_ResolveStop)
        {
            auto infos = GW2Api::FetchStatsInfos(
                std::vector<int>(statsIds.begin(), statsIds.end()));
            std::lock_guard<std::mutex> lk(g_Mx);
            for (auto& si : infos)
            {
                // Back-fill statsName in equipment pieces
                for (auto& e : g_Equipment)
                    for (auto& p : e.rawTab.pieces)
                        if (p.statsId == si.id && p.statsName.empty())
                            p.statsName = si.name;
            }
        }

        // Mark all resolved
        {
            std::lock_guard<std::mutex> lk(g_Mx);
            for (auto& b : g_Builds)    b.resolved = true;
            for (auto& e : g_Equipment) e.resolved = true;
        }

        // Fetch profession palette maps for any build that still needs a chat code
        {
            std::set<std::string> profsNeeded;
            {
                std::lock_guard<std::mutex> lk(g_Mx);
                for (auto& b : g_Builds)
                    if (b.chatCode.empty() && !b.profession.empty())
                        profsNeeded.insert(b.profession);
            }

            for (auto& prof : profsNeeded)
            {
                if (g_ResolveStop) break;
                if (g_ProfPalette.count(prof)) continue; // already cached
                auto palette = GW2Api::FetchProfessionPalette(prof);
                std::lock_guard<std::mutex> lk(g_Mx);
                g_ProfPalette[prof] = std::move(palette);
            }

            // Now fill in palette skill IDs for builds that have API skill IDs
            // but no palette IDs yet, then generate chat codes.
            std::lock_guard<std::mutex> lk(g_Mx);
            for (auto& b : g_Builds)
            {
                if (!b.chatCode.empty()) continue;
                auto pit = g_ProfPalette.find(b.profession);
                if (pit == g_ProfPalette.end()) continue;

                // Build inverted map: api_skill_id -> palette_id
                std::unordered_map<int,int> apiToPal;
                for (auto& [palId, apiId] : pit->second)
                    apiToPal[apiId] = palId;

                auto mapSkill = [&](int apiId) -> uint16_t {
                    if (!apiId) return 0;
                    auto it2 = apiToPal.find(apiId);
                    return (it2 != apiToPal.end()) ? (uint16_t)it2->second : 0;
                };

                auto& sk = b.rawTab.skills;
                b.paletteSkills[0]  = mapSkill(sk.heal);
                b.paletteSkills[1]  = 0; // aquatic heal unknown
                b.paletteSkills[2]  = mapSkill(sk.utilities[0]);
                b.paletteSkills[3]  = 0;
                b.paletteSkills[4]  = mapSkill(sk.utilities[1]);
                b.paletteSkills[5]  = 0;
                b.paletteSkills[6]  = mapSkill(sk.utilities[2]);
                b.paletteSkills[7]  = 0;
                b.paletteSkills[8]  = mapSkill(sk.elite);
                b.paletteSkills[9]  = 0;

                if (b.profession == "Ranger")
                {
                    b.rangerPets[0] = (uint8_t)sk.terrestrialPet1;
                    b.rangerPets[1] = (uint8_t)sk.terrestrialPet2;
                    b.rangerPets[2] = (uint8_t)sk.aquaticPet1;
                    b.rangerPets[3] = (uint8_t)sk.aquaticPet2;
                }
                if (b.profession == "Revenant")
                {
                    b.revenantLegends[0] = (uint8_t)sk.legend1;
                    b.revenantLegends[1] = (uint8_t)sk.legend2;
                }

                TryGenerateChatCode(b);
            }
        }

        TemplateStore::Save();
        g_ResolvePending = false;
    }

} // anonymous namespace

// ── Exported mutex ────────────────────────────────────────────────────────────
std::mutex TemplateStore::g_Mutex;

// ── Public API ────────────────────────────────────────────────────────────────

void TemplateStore::Init()
{
    Load();
}

void TemplateStore::Shutdown()
{
    g_ResolveStop = true;
    if (g_ResolveThread.joinable())
        g_ResolveThread.join();
    Save();
}

void TemplateStore::Load()
{
    std::string path = StorePath();
    if (path.empty()) return;

    std::ifstream f(path);
    if (!f.is_open()) return;

    try
    {
        json root = json::parse(f);

        std::lock_guard<std::mutex> lk(g_Mx);

        // Global caches
        if (root.contains("spec_names") && root["spec_names"].is_object())
            for (auto& [k, v] : root["spec_names"].items())
                if (v.is_string()) g_SpecNames[std::stoi(k)] = v.get<std::string>();

        if (root.contains("skill_names") && root["skill_names"].is_object())
            for (auto& [k, v] : root["skill_names"].items())
                if (v.is_string()) g_SkillNames[std::stoi(k)] = v.get<std::string>();

        if (root.contains("trait_names") && root["trait_names"].is_object())
            for (auto& [k, v] : root["trait_names"].items())
                if (v.is_string()) g_TraitNames[std::stoi(k)] = v.get<std::string>();

        if (root.contains("item_names") && root["item_names"].is_object())
            for (auto& [k, v] : root["item_names"].items())
                if (v.is_string()) g_ItemNames[std::stoi(k)] = v.get<std::string>();

        if (root.contains("item_rarities") && root["item_rarities"].is_object())
            for (auto& [k, v] : root["item_rarities"].items())
                if (v.is_string()) g_ItemRarities[std::stoi(k)] = v.get<std::string>();

        // Build templates
        if (root.contains("builds") && root["builds"].is_array())
        {
            for (auto& jb : root["builds"])
            {
                StoredBuildTemplate tmpl;
                tmpl.id            = jb.value("id",            std::string{});
                tmpl.label         = jb.value("label",         std::string{});
                tmpl.characterName = jb.value("character_name", std::string{});
                tmpl.profession    = jb.value("profession",    std::string{});
                tmpl.savedAt       = jb.value("saved_at",      (int64_t)0);
                tmpl.resolved      = jb.value("resolved",      false);
                if (jb.contains("raw_tab")) tmpl.rawTab = DeserialiseBuildTab(jb["raw_tab"]);
                // Chat-code data
                tmpl.chatCode = jb.value("chat_code", std::string{});
                if (jb.contains("palette_skills") && jb["palette_skills"].is_array())
                    for (int i = 0; i < 10 && i < (int)jb["palette_skills"].size(); ++i)
                        tmpl.paletteSkills[i] = (uint16_t)jb["palette_skills"][i].get<int>();
                if (jb.contains("spec_choices") && jb["spec_choices"].is_array())
                    for (int s = 0; s < 3 && s < (int)jb["spec_choices"].size(); ++s)
                        if (jb["spec_choices"][s].is_array())
                            for (int t = 0; t < 3 && t < (int)jb["spec_choices"][s].size(); ++t)
                                tmpl.specTraitChoices[s][t] = (uint8_t)jb["spec_choices"][s][t].get<int>();
                g_Builds.push_back(std::move(tmpl));
            }
        }

        // Equipment templates
        if (root.contains("equipment") && root["equipment"].is_array())
        {
            for (auto& je : root["equipment"])
            {
                StoredEquipmentTemplate tmpl;
                tmpl.id            = je.value("id",            std::string{});
                tmpl.label         = je.value("label",         std::string{});
                tmpl.characterName = je.value("character_name", std::string{});
                tmpl.profession    = je.value("profession",    std::string{});
                tmpl.savedAt       = je.value("saved_at",      (int64_t)0);
                tmpl.resolved      = je.value("resolved",      false);
                if (je.contains("raw_tab")) tmpl.rawTab = DeserialiseEquipTab(je["raw_tab"]);
                g_Equipment.push_back(std::move(tmpl));
            }
        }
    }
    catch (...) {}
}

void TemplateStore::Save()
{
    std::string path = StorePath();
    if (path.empty()) return;

    json root;

    std::lock_guard<std::mutex> lk(g_Mx);

    // Global caches
    json specNamesJ, skillNamesJ, traitNamesJ, itemNamesJ, itemRaritiesJ;
    for (auto& [k, v] : g_SpecNames)    specNamesJ[std::to_string(k)]    = v;
    for (auto& [k, v] : g_SkillNames)   skillNamesJ[std::to_string(k)]   = v;
    for (auto& [k, v] : g_TraitNames)   traitNamesJ[std::to_string(k)]   = v;
    for (auto& [k, v] : g_ItemNames)    itemNamesJ[std::to_string(k)]    = v;
    for (auto& [k, v] : g_ItemRarities) itemRaritiesJ[std::to_string(k)] = v;
    root["spec_names"]    = specNamesJ;
    root["skill_names"]   = skillNamesJ;
    root["trait_names"]   = traitNamesJ;
    root["item_names"]    = itemNamesJ;
    root["item_rarities"] = itemRaritiesJ;

    // Build templates
    json buildsArr = json::array();
    for (auto& b : g_Builds)
    {
        json jb;
        jb["id"]            = b.id;
        jb["label"]         = b.label;
        jb["character_name"] = b.characterName;
        jb["profession"]    = b.profession;
        jb["saved_at"]      = b.savedAt;
        jb["resolved"]      = b.resolved;
        jb["raw_tab"]       = SerialiseBuildTab(b.rawTab);
        // Chat-code data
        jb["chat_code"]      = b.chatCode;
        json palArr = json::array();
        for (auto p : b.paletteSkills) palArr.push_back((int)p);
        jb["palette_skills"] = palArr;
        json choicesArr = json::array();
        for (int s = 0; s < 3; ++s)
        {
            json tier = json::array();
            for (int t = 0; t < 3; ++t) tier.push_back((int)b.specTraitChoices[s][t]);
            choicesArr.push_back(tier);
        }
        jb["spec_choices"] = choicesArr;
        buildsArr.push_back(jb);
    }
    root["builds"] = buildsArr;

    // Equipment templates
    json equipArr = json::array();
    for (auto& e : g_Equipment)
    {
        json je;
        je["id"]            = e.id;
        je["label"]         = e.label;
        je["character_name"] = e.characterName;
        je["profession"]    = e.profession;
        je["saved_at"]      = e.savedAt;
        je["resolved"]      = e.resolved;
        je["raw_tab"]       = SerialiseEquipTab(e.rawTab);
        equipArr.push_back(je);
    }
    root["equipment"] = equipArr;

    std::ofstream f(path);
    if (f.is_open()) f << root.dump(4);
}

// ── Build template API ────────────────────────────────────────────────────────

std::vector<TemplateStore::StoredBuildTemplate> TemplateStore::GetAllBuilds()
{
    std::lock_guard<std::mutex> lk(g_Mx);
    return g_Builds;
}

std::vector<TemplateStore::StoredBuildTemplate> TemplateStore::GetBuilds(
    const std::string& characterName)
{
    std::lock_guard<std::mutex> lk(g_Mx);
    std::vector<StoredBuildTemplate> out;
    for (auto& b : g_Builds)
        if (b.characterName == characterName) out.push_back(b);
    return out;
}

std::string TemplateStore::SaveBuild(StoredBuildTemplate tmpl)
{
    if (tmpl.id.empty())
        tmpl.id = MakeId(tmpl.characterName + "_build");

    if (tmpl.savedAt == 0)
    {
        tmpl.savedAt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    {
        std::lock_guard<std::mutex> lk(g_Mx);
        // Replace if same ID exists
        for (auto& b : g_Builds)
        {
            if (b.id == tmpl.id) { b = std::move(tmpl); return b.id; }
        }
        g_Builds.push_back(std::move(tmpl));
    }

    Save();
    return g_Builds.back().id;
}

void TemplateStore::DeleteBuild(const std::string& id)
{
    {
        std::lock_guard<std::mutex> lk(g_Mx);
        g_Builds.erase(
            std::remove_if(g_Builds.begin(), g_Builds.end(),
                           [&](auto& b){ return b.id == id; }),
            g_Builds.end());
    }
    Save();
}

void TemplateStore::RenameBuild(const std::string& id, const std::string& newLabel)
{
    {
        std::lock_guard<std::mutex> lk(g_Mx);
        for (auto& b : g_Builds)
            if (b.id == id) { b.label = newLabel; break; }
    }
    Save();
}

// ── Equipment template API ────────────────────────────────────────────────────

std::vector<TemplateStore::StoredEquipmentTemplate> TemplateStore::GetAllEquipment()
{
    std::lock_guard<std::mutex> lk(g_Mx);
    return g_Equipment;
}

std::vector<TemplateStore::StoredEquipmentTemplate> TemplateStore::GetEquipment(
    const std::string& characterName)
{
    std::lock_guard<std::mutex> lk(g_Mx);
    std::vector<StoredEquipmentTemplate> out;
    for (auto& e : g_Equipment)
        if (e.characterName == characterName) out.push_back(e);
    return out;
}

std::string TemplateStore::SaveEquipment(StoredEquipmentTemplate tmpl)
{
    if (tmpl.id.empty())
        tmpl.id = MakeId(tmpl.characterName + "_equip");

    if (tmpl.savedAt == 0)
    {
        tmpl.savedAt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    {
        std::lock_guard<std::mutex> lk(g_Mx);
        for (auto& e : g_Equipment)
        {
            if (e.id == tmpl.id) { e = std::move(tmpl); return e.id; }
        }
        g_Equipment.push_back(std::move(tmpl));
    }

    Save();
    return g_Equipment.back().id;
}

void TemplateStore::DeleteEquipment(const std::string& id)
{
    {
        std::lock_guard<std::mutex> lk(g_Mx);
        g_Equipment.erase(
            std::remove_if(g_Equipment.begin(), g_Equipment.end(),
                           [&](auto& e){ return e.id == id; }),
            g_Equipment.end());
    }
    Save();
}

void TemplateStore::RenameEquipment(const std::string& id, const std::string& newLabel)
{
    {
        std::lock_guard<std::mutex> lk(g_Mx);
        for (auto& e : g_Equipment)
            if (e.id == id) { e.label = newLabel; break; }
    }
    Save();
}

// ── Cache lookups ─────────────────────────────────────────────────────────────

std::string TemplateStore::GetSpecName(int id)
{
    std::lock_guard<std::mutex> lk(g_Mx);
    auto it = g_SpecNames.find(id);
    return (it != g_SpecNames.end()) ? it->second : "";
}

std::string TemplateStore::GetSkillName(int id)
{
    std::lock_guard<std::mutex> lk(g_Mx);
    auto it = g_SkillNames.find(id);
    return (it != g_SkillNames.end()) ? it->second : "";
}

std::string TemplateStore::GetItemName(int id)
{
    std::lock_guard<std::mutex> lk(g_Mx);
    auto it = g_ItemNames.find(id);
    return (it != g_ItemNames.end()) ? it->second : "";
}

std::string TemplateStore::GetItemRarity(int id)
{
    std::lock_guard<std::mutex> lk(g_Mx);
    auto it = g_ItemRarities.find(id);
    return (it != g_ItemRarities.end()) ? it->second : "";
}

std::string TemplateStore::GetTraitName(int id)
{
    std::lock_guard<std::mutex> lk(g_Mx);
    auto it = g_TraitNames.find(id);
    return (it != g_TraitNames.end()) ? it->second : "";
}

std::array<int,9> TemplateStore::GetSpecMajorTraits(int specId)
{
    std::lock_guard<std::mutex> lk(g_Mx);
    auto it = g_SpecMajorTraits.find(specId);
    if (it != g_SpecMajorTraits.end()) return it->second;
    return {};
}

int TemplateStore::PaletteToApi(const std::string& profession, int paletteId)
{
    std::lock_guard<std::mutex> lk(g_Mx);
    auto pit = g_ProfPalette.find(profession);
    if (pit == g_ProfPalette.end()) return 0;
    auto it = pit->second.find(paletteId);
    return (it != pit->second.end()) ? it->second : 0;
}

int TemplateStore::ApiToPalette(const std::string& profession, int apiSkillId)
{
    std::lock_guard<std::mutex> lk(g_Mx);
    auto pit = g_ProfPalette.find(profession);
    if (pit == g_ProfPalette.end()) return 0;
    for (auto& [palId, apiId] : pit->second)
        if (apiId == apiSkillId) return palId;
    return 0;
}

std::string TemplateStore::SaveBuildByLabel(StoredBuildTemplate tmpl)
{
    // Find existing build with same label + character, replace it
    {
        std::lock_guard<std::mutex> lk(g_Mx);
        for (auto& b : g_Builds)
        {
            if (b.characterName == tmpl.characterName && b.label == tmpl.label)
            {
                tmpl.id      = b.id;      // reuse old ID
                tmpl.savedAt = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                b = std::move(tmpl);
                Save();
                return b.id;
            }
        }
    }
    // Not found — use normal save path
    return SaveBuild(std::move(tmpl));
}

std::string TemplateStore::SaveEquipmentByLabel(StoredEquipmentTemplate tmpl)
{
    {
        std::lock_guard<std::mutex> lk(g_Mx);
        for (auto& e : g_Equipment)
        {
            if (e.characterName == tmpl.characterName && e.label == tmpl.label)
            {
                tmpl.id      = e.id;
                tmpl.savedAt = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                e = std::move(tmpl);
                Save();
                return e.id;
            }
        }
    }
    return SaveEquipment(std::move(tmpl));
}

void TemplateStore::RequestResolve(const std::string& apiKey)
{
    if (g_ResolvePending.exchange(true)) return;  // already pending

    g_ResolveApiKey = apiKey;
    g_ResolveStop   = false;

    if (g_ResolveThread.joinable())
        g_ResolveThread.join();

    g_ResolveThread = std::thread(ResolveWorker, apiKey);
    g_ResolveThread.detach();   // fire-and-forget (g_ResolvePending tracks it)
}
