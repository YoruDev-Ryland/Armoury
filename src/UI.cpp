#include "UI.h"
#include "Settings.h"
#include "TemplateStore.h"
#include "ChatCode.h"
#include "GW2Api.h"
#include "Shared.h"

#include <imgui.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <sstream>
#include <cstring>
#include <ctime>
#include <unordered_set>
#include <mutex>
#include <windows.h>
#include <fstream>

// ── Colour helpers ────────────────────────────────────────────────────────────

static ImU32 RarityColor(const std::string& rarity)
{
    if (rarity == "Junk")        return IM_COL32(170, 170, 170, 255);
    if (rarity == "Basic")       return IM_COL32(255, 255, 255, 255);
    if (rarity == "Fine")        return IM_COL32(102, 153, 255, 255);
    if (rarity == "Masterwork")  return IM_COL32( 26, 147,   6, 255);
    if (rarity == "Rare")        return IM_COL32(250, 183,   0, 255);
    if (rarity == "Exotic")      return IM_COL32(200,  96,  10, 255);
    if (rarity == "Ascended")    return IM_COL32(251,  62, 141, 255);
    if (rarity == "Legendary")   return IM_COL32(172, 105, 255, 255);
    return IM_COL32(200, 200, 200, 255);
}

static ImVec4 RarityColorV4(const std::string& rarity)
{
    ImU32 c = RarityColor(rarity);
    return ImVec4(
        ((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.f,
        ((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.f,
        ((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.f,
        1.f);
}

// ── Profession colour ─────────────────────────────────────────────────────────

static ImVec4 ProfessionColor(const std::string& prof)
{
    if (prof == "Guardian")    return ImVec4(0.28f, 0.55f, 0.96f, 1.0f);
    if (prof == "Warrior")     return ImVec4(1.00f, 0.81f, 0.00f, 1.0f);
    if (prof == "Engineer")    return ImVec4(0.60f, 0.42f, 0.22f, 1.0f);
    if (prof == "Ranger")      return ImVec4(0.37f, 0.69f, 0.27f, 1.0f);
    if (prof == "Thief")       return ImVec4(0.47f, 0.47f, 0.47f, 1.0f);
    if (prof == "Elementalist")return ImVec4(0.93f, 0.36f, 0.27f, 1.0f);
    if (prof == "Mesmer")      return ImVec4(0.73f, 0.36f, 0.73f, 1.0f);
    if (prof == "Necromancer") return ImVec4(0.25f, 0.64f, 0.42f, 1.0f);
    if (prof == "Revenant")    return ImVec4(0.61f, 0.20f, 0.20f, 1.0f);
    return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
}

// ── Name resolution helpers ───────────────────────────────────────────────────

static std::string SkillDisplay(int id)
{
    if (!id) return "(none)";
    std::string n = TemplateStore::GetSkillName(id);
    if (!n.empty()) return n;
    return "#" + std::to_string(id);
}

static std::string SpecDisplay(int id)
{
    if (!id) return "(none)";
    std::string n = TemplateStore::GetSpecName(id);
    if (!n.empty()) return n;
    return "#" + std::to_string(id);
}

static std::string TraitDisplay(int id)
{
    if (!id) return "";
    std::string n = TemplateStore::GetTraitName(id);
    if (!n.empty()) return n;
    return "#" + std::to_string(id);
}

static std::string ItemDisplay(int id)
{
    if (!id) return "(none)";
    std::string n = TemplateStore::GetItemName(id);
    if (!n.empty()) return n;
    return "Item #" + std::to_string(id);
}

// ── Icon cache ────────────────────────────────────────────────────────────────
//
// Download thread:   fetches PNG bytes, saves to disk, pushes into s_PendingUploads
// Render thread:     each frame drains s_PendingUploads via FlushPendingIcons(),
//                    calls Textures_GetOrCreateFromMemory (D3D11 upload must be
//                    on the render thread).
//
// On subsequent sessions the file already exists → Textures_GetOrCreateFromFile
// (async one-shot load, also render-thread-safe).

struct PendingIcon { std::string url; std::vector<uint8_t> bytes; };

static std::string                   s_IconsDir;
static std::mutex                    s_IconMx;
static std::unordered_set<std::string> s_IconDownloading;   // urls in-flight
static std::vector<PendingIcon>        s_PendingUploads;    // ready for GPU upload

// Return (and lazily create) the icons directory.
static const std::string& IconsDir()
{
    if (!s_IconsDir.empty()) return s_IconsDir;
    if (!APIDefs) return s_IconsDir;
    s_IconsDir = std::string(APIDefs->Paths_GetAddonDirectory("Armoury")) + "\\icons";
    CreateDirectoryA(s_IconsDir.c_str(), nullptr);
    return s_IconsDir;
}

// Filename for a URL = last path component.
static std::string IconFilename(const std::string& url)
{
    size_t pos = url.rfind('/');
    if (pos != std::string::npos && pos + 1 < url.size())
        return url.substr(pos + 1);
    std::string s = url;
    for (char& c : s) if (!isalnum((unsigned char)c) && c != '.') c = '_';
    return s + ".png";
}

// Called ONCE PER FRAME from the render thread before any icon draws.
// Drains s_PendingUploads and uploads each to Nexus.
static void FlushPendingIcons()
{
    if (!APIDefs) return;
    std::vector<PendingIcon> batch;
    {
        std::lock_guard<std::mutex> lk(s_IconMx);
        batch.swap(s_PendingUploads);
    }
    for (auto& pi : batch)
    {
        APIDefs->Textures_GetOrCreateFromMemory(
            pi.url.c_str(),
            (void*)pi.bytes.data(),
            (uint64_t)pi.bytes.size());
    }
}

// Return an ImTextureID for a GW2 render CDN URL, or nullptr while loading.
static ImTextureID GetIconTexture(const std::string& url)
{
    if (url.empty() || !APIDefs) return nullptr;

    // Already registered and uploaded — fastest path (zero allocations).
    Texture_t* tex = APIDefs->Textures_Get(url.c_str());
    if (tex && tex->Resource) return (ImTextureID)tex->Resource;

    const std::string& dir = IconsDir();
    if (dir.empty()) return nullptr;
    std::string path = dir + "\\" + IconFilename(url);

    // File on disk from a previous session → ask Nexus to load it (async).
    // Nexus deduplicates by id, so this is a no-op after the first call.
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES)
    {
        tex = APIDefs->Textures_GetOrCreateFromFile(url.c_str(), path.c_str());
        return (tex && tex->Resource) ? (ImTextureID)tex->Resource : nullptr;
    }

    // Not on disk — fire a background download.
    {
        std::lock_guard<std::mutex> lk(s_IconMx);
        if (s_IconDownloading.count(url)) return nullptr;
        s_IconDownloading.insert(url);
    }

    std::thread([url, path]{
        auto bytes = GW2Api::DownloadBytesFromURL(url);
        if (!bytes.empty())
        {
            // Save to disk for next session.
            std::ofstream f(path, std::ios::binary);
            if (f) f.write(reinterpret_cast<const char*>(bytes.data()),
                           (std::streamsize)bytes.size());

            // Queue GPU upload — MUST happen on the render thread (see FlushPendingIcons).
            std::lock_guard<std::mutex> lk(s_IconMx);
            s_PendingUploads.push_back({url, std::move(bytes)});
        }
        {
            std::lock_guard<std::mutex> lk(s_IconMx);
            s_IconDownloading.erase(url);
        }
    }).detach();

    return nullptr;
}

// Draw an icon at the given square size, or a grey rounded placeholder.
static void IconImage(const std::string& url, float size)
{
    ImTextureID tex = GetIconTexture(url);
    if (tex)
    {
        ImGui::Image(tex, ImVec2(size, size));
    }
    else
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            p, ImVec2(p.x + size, p.y + size), IM_COL32(55, 55, 55, 210), 4.f);
        ImGui::Dummy(ImVec2(size, size));
    }
}

// ── UI state ──────────────────────────────────────────────────────────────────

namespace
{
    // Character list
    std::vector<std::string>  s_Characters;
    std::atomic_bool          s_CharactersFetching{false};
    std::string               s_CharactersStatus;

    std::string               s_SelectedChar;

    // Selected template IDs
    std::string               s_SelectedBuildId;
    std::string               s_SelectedEquipId;
    bool                      s_LastClickedIsBuild = true; // for rename/delete target

    // ── Delete confirmation ───────────────────────────────────────────────────
    bool        s_ShowDeleteBuildConfirm = false;
    bool        s_ShowDeleteEquipConfirm = false;
    std::string s_PendingDeleteId;
    std::string s_PendingDeleteLabel;

    // ── Rename modal ──────────────────────────────────────────────────────────
    bool        s_ShowRenameBuild = false;
    bool        s_ShowRenameEquip = false;
    std::string s_RenameTargetId;
    char        s_RenameBuf[128]  = {};

    // ── Import from GW2 state ─────────────────────────────────────────────────
    enum class ImportPhase { Idle, Fetching, Ready, Error };

    bool                               s_ShowImportBuild  = false;
    bool                               s_ShowImportEquip  = false;
    ImportPhase                        s_ImportPhase      = ImportPhase::Idle;
    std::string                        s_ImportStatus;
    std::vector<GW2Api::BuildTab>      s_ImportedBuildTabs;
    std::vector<GW2Api::EquipmentTab>  s_ImportedEquipTabs;

    // ── Import from code state ─────────────────────────────────────────────────
    bool        s_ShowImportCode    = false;
    char        s_CodeBuf[512]      = {};
    std::string s_CodeParseStatus;
    bool        s_CodeParsed        = false;
    std::string s_ParsedProfession;
    int         s_ParsedSpecIds[3]        = {};
    uint8_t     s_ParsedSpecChoices[3][3] = {};
    uint16_t    s_ParsedPaletteSkills[10] = {};
    char        s_CodeLabelBuf[128]       = {};

    // Slot helpers
    // ── Background fetch helpers ──────────────────────────────────────────────

    void FetchCharactersAsync()
    {
        if (s_CharactersFetching.exchange(true)) return;
        s_CharactersStatus = "Fetching...";
        std::thread([]{
            auto chars = GW2Api::FetchCharacters(g_Settings.ApiKey);
            std::sort(chars.begin(), chars.end());
            s_Characters       = std::move(chars);
            s_CharactersStatus = s_Characters.empty() ? "No characters found." : "";
            s_CharactersFetching = false;
        }).detach();
    }

    void FetchBuildTabsAsync(const std::string& charName)
    {
        if (s_ImportPhase == ImportPhase::Fetching) return;
        s_ImportPhase  = ImportPhase::Fetching;
        s_ImportStatus = "Fetching build tabs...";
        s_ImportedBuildTabs.clear();
        std::thread([charName]{
            auto tabs = GW2Api::FetchBuildTabs(g_Settings.ApiKey, charName);
            if (tabs.empty()) {
                s_ImportStatus = "No build tabs returned. Check API key has 'builds' permission.";
                s_ImportPhase  = ImportPhase::Error;
            } else {
                s_ImportedBuildTabs = std::move(tabs);
                s_ImportPhase       = ImportPhase::Ready;
                s_ImportStatus.clear();
                TemplateStore::RequestResolve(g_Settings.ApiKey);
            }
        }).detach();
    }

    void FetchEquipTabsAsync(const std::string& charName)
    {
        if (s_ImportPhase == ImportPhase::Fetching) return;
        s_ImportPhase  = ImportPhase::Fetching;
        s_ImportStatus = "Fetching equipment tabs...";
        s_ImportedEquipTabs.clear();
        std::thread([charName]{
            auto tabs = GW2Api::FetchEquipmentTabs(g_Settings.ApiKey, charName);
            if (tabs.empty()) {
                s_ImportStatus = "No equipment tabs returned. Check API key permissions.";
                s_ImportPhase  = ImportPhase::Error;
            } else {
                s_ImportedEquipTabs = std::move(tabs);
                s_ImportPhase       = ImportPhase::Ready;
                s_ImportStatus.clear();
                TemplateStore::RequestResolve(g_Settings.ApiKey);
            }
        }).detach();
    }

    // ── Helper: current character name ────────────────────────────────────────

    std::string ActiveChar()
    {
        if (g_Settings.ShowOnlyCurrentChar && MumbleIdent)
            return MumbleIdent->Name;
        return s_SelectedChar;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ── Build detail view ─────────────────────────────────────────────────────
    // ─────────────────────────────────────────────────────────────────────────

    void DrawBuildDetail(const TemplateStore::StoredBuildTemplate& b)
    {
        auto& tab = b.rawTab;

        // ── Header: profession + chat code ────────────────────────────────────
        ImGui::TextColored(ProfessionColor(b.profession), "%s", b.profession.c_str());
        if (!b.chatCode.empty())
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f,0.12f,0.12f,1));
            char codeBuf[512];
            std::strncpy(codeBuf, b.chatCode.c_str(), sizeof(codeBuf)-1);
            ImGui::InputText("##code_ro", codeBuf, sizeof(codeBuf),
                             ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(chat code pending resolution...)");
        }

        // ── Save date + GW2 build stamp ───────────────────────────────────────
        if (b.savedAt > 0 || b.gw2BuildId > 0)
        {
            if (b.savedAt > 0)
            {
                std::time_t t = (std::time_t)b.savedAt;
                char dateBuf[32] = {};
                std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", std::localtime(&t));
                ImGui::TextDisabled("Saved: %s", dateBuf);
                if (b.gw2BuildId > 0)
                { ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine(); }
            }
            if (b.gw2BuildId > 0)
            {
                int cur = MumbleLink ? (int)MumbleLink->Context.BuildId : 0;
                if (cur > 0 && cur != b.gw2BuildId)
                    ImGui::TextColored(ImVec4(1.f, 0.65f, 0.1f, 1.f),
                        "GW2 Build #%d  (current: #%d \xe2\x80\x94 may be outdated!)",
                        b.gw2BuildId, cur);
                else
                    ImGui::TextDisabled("GW2 Build #%d", b.gw2BuildId);
            }
        }

        ImGui::Separator();

        // ── Specialization traitlines ─────────────────────────────────────────
        // Each spec is its own full-width row (stacked vertically):
        //   [spec icon]  [spec name]
        //   indent  │  [tier1: 3 icons stacked]  [tier2: 3 icons]  [tier3: 3 icons]
        // Selected trait = bright + highlight border; unselected = dimmed.
        constexpr float kSpecIconSz  = 24.f;
        constexpr float kTraitIconSz = 24.f;   // each trait icon square
        constexpr float kTierGap     = 8.f;    // horizontal gap between tier columns
        constexpr float kTraitSpacing = 2.f;   // vertical gap between stacked icons

        bool hasAnySpec = false;
        for (int si = 0; si < 3; ++si) if (tab.specs[si].id) { hasAnySpec = true; break; }

        if (hasAnySpec)
        {
            for (int si = 0; si < 3; ++si)
            {
                auto& spec = tab.specs[si];
                if (!spec.id) continue;

                ImGui::PushID(si + 5000);

                // ── Spec header: icon + name ──────────────────────────────────
                {
                    ImTextureID specIco = GetIconTexture(TemplateStore::GetSpecIcon(spec.id));
                    if (specIco)
                        ImGui::Image(specIco, ImVec2(kSpecIconSz, kSpecIconSz));
                    else
                    {
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddRectFilled(
                            p, ImVec2(p.x + kSpecIconSz, p.y + kSpecIconSz),
                            IM_COL32(60,60,60,200), 3.f);
                        ImGui::Dummy(ImVec2(kSpecIconSz, kSpecIconSz));
                    }
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(ProfessionColor(b.profession), "%s",
                                       SpecDisplay(spec.id).c_str());
                }

                // ── Trait columns ─────────────────────────────────────────────
                // Three tier-columns, each with 3 small icons stacked vertically.
                auto majorTraits = TemplateStore::GetSpecMajorTraits(spec.id);

                float indent = kSpecIconSz + ImGui::GetStyle().ItemSpacing.x;
                ImGui::Indent(indent);

                for (int tier = 0; tier < 3; ++tier)
                {
                    if (tier > 0) ImGui::SameLine(0.f, kTierGap);

                    ImGui::BeginGroup();
                    for (int choice = 0; choice < 3; ++choice)
                    {
                        if (choice > 0)
                        {
                            // push each icon down by kTraitSpacing manually
                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kTraitSpacing);
                        }

                        int  traitId  = (int)majorTraits.size() > tier * 3 + choice
                                        ? majorTraits[tier * 3 + choice] : 0;
                        bool selected = traitId && (traitId == spec.traits[tier]);

                        ImVec4 bg   = selected
                            ? ImVec4(0.15f, 0.38f, 0.72f, 1.f)
                            : ImVec4(0.08f, 0.08f, 0.08f, 0.95f);
                        ImVec4 tint = selected
                            ? ImVec4(1.f, 1.f, 1.f, 1.f)
                            : ImVec4(0.38f, 0.38f, 0.38f, 0.90f);

                        ImTextureID ico = traitId
                            ? GetIconTexture(TemplateStore::GetTraitIcon(traitId))
                            : nullptr;

                        ImGui::PushID(tier * 10 + choice);
                        if (ico)
                        {
                            int   pad = selected ? 1 : 0;
                            float isz = kTraitIconSz - (float)pad * 2.f;
                            ImGui::ImageButton(ico, ImVec2(isz, isz),
                                               ImVec2(0,0), ImVec2(1,1),
                                               pad, bg, tint);
                        }
                        else
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button, bg);
                            ImGui::Button("##t", ImVec2(kTraitIconSz, kTraitIconSz));
                            ImGui::PopStyleColor();
                        }
                        if (traitId && ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", TraitDisplay(traitId).c_str());
                        ImGui::PopID();
                    }
                    ImGui::EndGroup();
                }

                ImGui::Unindent(indent);
                ImGui::PopID();

                if (si < 2) ImGui::Spacing();
            }
            ImGui::Spacing();
        }

        // ── Skill bar ─────────────────────────────────────────────────────────
        // Five skill slots evenly distributed across the full available width.
        // Sizes recalculate every frame so they adapt to window resizing.
        struct SkillSlot { int id; const char* label; ImVec4 color; };
        SkillSlot slots[5] = {
            { tab.skills.heal,         "Heal",    ImVec4(0.30f,0.78f,0.30f,1.f) },
            { tab.skills.utilities[0], "Utility", ImVec4(0.38f,0.58f,0.90f,1.f) },
            { tab.skills.utilities[1], "Utility", ImVec4(0.38f,0.58f,0.90f,1.f) },
            { tab.skills.utilities[2], "Utility", ImVec4(0.38f,0.58f,0.90f,1.f) },
            { tab.skills.elite,        "Elite",   ImVec4(0.85f,0.28f,0.28f,1.f) },
        };

        {
            float avail  = ImGui::GetContentRegionAvail().x;
            float gapW   = ImGui::GetStyle().ItemSpacing.x;
            // 5 slots with 4 gaps between them
            float slotW  = std::floor((avail - gapW * 4.f) / 5.f);
            // Icon fills the slot width but capped so it doesn't become absurd
            float iconSz = std::min(slotW - 4.f, 52.f);
            iconSz = std::max(iconSz, 20.f);

            for (int i = 0; i < 5; ++i)
            {
                auto& sl = slots[i];
                ImGui::PushID(i + 9000);
                ImGui::BeginGroup();

                // Role label: centred in slot
                {
                    float lw = ImGui::CalcTextSize(sl.label).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                          std::max(0.f, (slotW - lw) * 0.5f));
                    ImGui::TextColored(sl.color, "%s", sl.label);
                }

                // Icon (or grey placeholder): centred in slot
                {
                    float iconOff = std::max(0.f, (slotW - iconSz) * 0.5f);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + iconOff);
                    ImTextureID ico = sl.id
                        ? GetIconTexture(TemplateStore::GetSkillIcon(sl.id)) : nullptr;
                    if (ico)
                        ImGui::Image(ico, ImVec2(iconSz, iconSz));
                    else
                    {
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddRectFilled(
                            p, ImVec2(p.x + iconSz, p.y + iconSz),
                            IM_COL32(50,50,50,220), 4.f);
                        ImGui::Dummy(ImVec2(iconSz, iconSz));
                    }
                }

                // Skill name: centred, word-wrapped to slot width
                {
                    std::string name = sl.id ? SkillDisplay(sl.id) : std::string("(none)");
                    float nw = ImGui::CalcTextSize(name.c_str(), nullptr, false, slotW).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                        std::max(0.f, (slotW - std::min(nw, slotW)) * 0.5f));
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + slotW);
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::PopTextWrapPos();
                }

                ImGui::EndGroup();
                if (sl.id && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", SkillDisplay(sl.id).c_str());
                ImGui::PopID();
                if (i < 4) ImGui::SameLine(0.f, gapW);
            }
        }

        if (tab.skills.terrestrialPet1 || tab.skills.terrestrialPet2)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Pets");
            ImGui::Indent(12.f);
            if (tab.skills.terrestrialPet1)
                ImGui::BulletText("Pet 1: #%d", tab.skills.terrestrialPet1);
            if (tab.skills.terrestrialPet2)
                ImGui::BulletText("Pet 2: #%d", tab.skills.terrestrialPet2);
            ImGui::Unindent(12.f);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ── Equipment detail view ─────────────────────────────────────────────────
    // ─────────────────────────────────────────────────────────────────────────

    void DrawEquipDetail(const TemplateStore::StoredEquipmentTemplate& e)
    {
        auto& tab = e.rawTab;

        // Find a piece by API slot name
        auto FindPiece = [&](const char* slot) -> const GW2Api::EquipmentPiece*
        {
            for (auto& p : tab.pieces) if (p.slot == slot) return &p;
            return nullptr;
        };

        // Render item cell with rarity colour
        auto ItemCell = [&](int id)
        {
            std::string rar = TemplateStore::GetItemRarity(id);
            if (!rar.empty())
                ImGui::TextColored(RarityColorV4(rar), "%s", ItemDisplay(id).c_str());
            else
                ImGui::TextUnformatted(ItemDisplay(id).c_str());
        };

        // Render a text cell — grey dash when empty
        auto TextCell = [&](const std::string& s)
        {
            if (s.empty()) ImGui::TextDisabled("-");
            else           ImGui::TextUnformatted(s.c_str());
        };

        // Upgrade name by index; empty if absent
        auto UpgradeName = [&](const GW2Api::EquipmentPiece& p, int idx) -> std::string
        {
            if (idx < (int)p.upgradeIds.size()) return ItemDisplay(p.upgradeIds[idx]);
            return {};
        };

        // Infusions: newline-joined string
        auto InfusionStr = [&](const GW2Api::EquipmentPiece& p) -> std::string
        {
            std::string out;
            for (auto inf : p.infusionIds)
            { if (!out.empty()) out += "\n"; out += ItemDisplay(inf); }
            return out;
        };

        // ── Armour & Accessories ──────────────────────────────────────────────
        // GW2 API slot names: Helm, Shoulders, Coat, Gloves, Leggings, Boots,
        //                     Backpack, Accessory1/2, Amulet, Ring1/2
        struct ArmorRow { const char* apiSlot; const char* label; };
        constexpr ArmorRow kArmorOrder[] = {
            {"Helm",       "Head"},
            {"Shoulders",  "Shoulders"},
            {"Coat",       "Chest"},
            {"Gloves",     "Hands"},
            {"Leggings",   "Legs"},
            {"Boots",      "Feet"},
            {"Backpack",   "Back"},
            {"Accessory1", "Accessory 1"},
            {"Accessory2", "Accessory 2"},
            {"Amulet",     "Amulet"},
            {"Ring1",      "Ring 1"},
            {"Ring2",      "Ring 2"},
        };

        ImGui::TextDisabled("Armour & Accessories");
        if (ImGui::BeginTable("equip_armor", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("##ic",   ImGuiTableColumnFlags_WidthFixed,   30.f);
            ImGui::TableSetupColumn("Slot",   ImGuiTableColumnFlags_WidthFixed,   90.f);
            ImGui::TableSetupColumn("Item",   ImGuiTableColumnFlags_WidthStretch, 2.f);
            ImGui::TableSetupColumn("Stats",  ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("Rune",   ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("Infusion",ImGuiTableColumnFlags_WidthStretch,1.5f);
            ImGui::TableHeadersRow();

            for (auto& row : kArmorOrder)
            {
                const GW2Api::EquipmentPiece* piece = FindPiece(row.apiSlot);
                if (!piece) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                IconImage(TemplateStore::GetItemIcon(piece->itemId), 26.f);
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(row.label);
                ImGui::TableSetColumnIndex(2); ItemCell(piece->itemId);
                ImGui::TableSetColumnIndex(3); TextCell(piece->statsName);
                ImGui::TableSetColumnIndex(4); TextCell(UpgradeName(*piece, 0));
                ImGui::TableSetColumnIndex(5); TextCell(InfusionStr(*piece));
            }
            ImGui::EndTable();
        }

        // ── Weapons ───────────────────────────────────────────────────────────
        struct WeapRow { const char* apiSlot; const char* label; };
        constexpr WeapRow kWeapOrder[] = {
            {"WeaponA1", "Main (set A)"},
            {"WeaponA2", "Off  (set A)"},
            {"WeaponB1", "Main (set B)"},
            {"WeaponB2", "Off  (set B)"},
        };

        ImGui::Spacing();
        ImGui::TextDisabled("Weapons");
        if (ImGui::BeginTable("equip_weapons", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("##ic",    ImGuiTableColumnFlags_WidthFixed,   30.f);
            ImGui::TableSetupColumn("Slot",    ImGuiTableColumnFlags_WidthFixed,   90.f);
            ImGui::TableSetupColumn("Item",    ImGuiTableColumnFlags_WidthStretch, 2.f);
            ImGui::TableSetupColumn("Stats",   ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("Sigil",   ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("Infusion",ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableHeadersRow();

            bool anyWeapon = false;
            for (auto& row : kWeapOrder)
            {
                const GW2Api::EquipmentPiece* piece = FindPiece(row.apiSlot);
                if (!piece) continue;
                anyWeapon = true;
                // Two-handed weapons have sigil 1 at index 0 and sigil 2 at index 1
                std::string sigils = UpgradeName(*piece, 0);
                std::string s2     = UpgradeName(*piece, 1);
                if (!s2.empty()) { if (!sigils.empty()) sigils += "\n"; sigils += s2; }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                IconImage(TemplateStore::GetItemIcon(piece->itemId), 26.f);
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(row.label);
                ImGui::TableSetColumnIndex(2); ItemCell(piece->itemId);
                ImGui::TableSetColumnIndex(3); TextCell(piece->statsName);
                ImGui::TableSetColumnIndex(4); TextCell(sigils);
                ImGui::TableSetColumnIndex(5); TextCell(InfusionStr(*piece));
            }
            if (!anyWeapon)
            { ImGui::TableNextRow(); ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("(none)"); }
            ImGui::EndTable();
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ── Combined (linked) build + equipment detail ────────────────────────────
    // ─────────────────────────────────────────────────────────────────────────

    void DrawCombinedDetail(
        const TemplateStore::StoredBuildTemplate&     b,
        const TemplateStore::StoredEquipmentTemplate& e)
    {
        std::string bLabel = b.label.empty() ? b.rawTab.buildName : b.label;
        std::string eLabel = e.label.empty() ? e.rawTab.tabName   : e.label;
        ImGui::TextColored(ProfessionColor(b.profession), "%s  ->  %s",
                           bLabel.c_str(), eLabel.c_str());
        ImGui::Separator();
        DrawBuildDetail(b);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        DrawEquipDetail(e);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ── Character panel ───────────────────────────────────────────────────────
    // ─────────────────────────────────────────────────────────────────────────

    void DrawCharacterPanel(float panelWidth)
    {
        ImGui::BeginChild("##char_panel", ImVec2(panelWidth, 0), false);

        ImGui::TextDisabled("Characters");
        ImGui::Separator();

        if (s_CharactersFetching)
        {
            ImGui::TextDisabled("Fetching...");
        }
        else
        {
            for (auto& name : s_Characters)
            {
                bool sel = (name == s_SelectedChar);
                if (ImGui::Selectable(name.c_str(), sel,
                                      ImGuiSelectableFlags_None,
                                      ImVec2(panelWidth - 8.f, 0)))
                {
                    if (!sel)
                    {
                        s_SelectedChar    = name;
                        s_SelectedBuildId.clear();
                        s_SelectedEquipId.clear();
                    }
                }
            }

            if (s_Characters.empty())
            {
                if (g_Settings.ApiKey.empty())
                    ImGui::TextDisabled("Enter API key\nin Options.");
                else
                    ImGui::TextDisabled("%s", s_CharactersStatus.empty()
                        ? "No characters." : s_CharactersStatus.c_str());
            }
        }

        ImGui::Spacing();
        if (!g_Settings.ApiKey.empty())
            if (ImGui::SmallButton("Refresh"))
                FetchCharactersAsync();

        ImGui::EndChild();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ── Main (right) panel — unified builds + equipment ───────────────────────
    // ─────────────────────────────────────────────────────────────────────────

    void DrawMainPanel()
    {
        const std::string charName = ActiveChar();
        const bool hasChar = !charName.empty();
        const bool hasKey  = !g_Settings.ApiKey.empty();

        // ── Toolbar ───────────────────────────────────────────────────────────
        if (hasChar && hasKey)
        {
            if (ImGui::Button("Import Builds##gw2b"))
            {
                s_ShowImportBuild = true;
                s_ImportPhase     = ImportPhase::Idle;
                s_ImportedBuildTabs.clear();
                FetchBuildTabsAsync(charName);
            }
            ImGui::SameLine();
            if (ImGui::Button("Import Equipment##gw2e"))
            {
                s_ShowImportEquip = true;
                s_ImportPhase     = ImportPhase::Idle;
                s_ImportedEquipTabs.clear();
                FetchEquipTabsAsync(charName);
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("Import from Code"))
        {
            s_ShowImportCode  = true;
            s_CodeBuf[0]      = '\0';
            s_CodeLabelBuf[0] = '\0';
            s_CodeParseStatus.clear();
            s_CodeParsed      = false;
        }

        ImGui::Separator();

        if (!hasChar)
        {
            ImGui::TextDisabled(hasKey
                ? "Select a character from the list."
                : "Configure your GW2 API key in Options.");
            return;
        }

        auto builds = TemplateStore::GetBuilds(charName);
        auto equips = TemplateStore::GetEquipment(charName);

        // ── Side-by-side Equipment | Builds ───────────────────────────────────
        const float listH = 200.f;
        if (ImGui::BeginTable("##dual_lists", 2,
            ImGuiTableFlags_BordersInnerV, ImVec2(0, listH)))
        {
            ImGui::TableSetupColumn("Equipment", ImGuiTableColumnFlags_WidthStretch, 1.f);
            ImGui::TableSetupColumn("Builds",    ImGuiTableColumnFlags_WidthStretch, 1.f);
            ImGui::TableNextRow();

            // ── Equipment column ──────────────────────────────────────────────
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Equipment");
            ImGui::Separator();
            ImGui::BeginChild("##equip_col", ImVec2(0, listH - 32.f), false,
                              ImGuiWindowFlags_None);
            if (equips.empty())
                ImGui::TextDisabled("  None. Use 'Import Equipment'.");
            else for (auto& eq : equips)
            {
                ImGui::PushID(eq.id.c_str());
                bool sel = (eq.id == s_SelectedEquipId);
                std::string lbl = eq.label.empty() ? eq.rawTab.tabName : eq.label;
                if (lbl.empty()) lbl = "Equipment";

                std::string linkedBid = TemplateStore::GetLinkedBuild(eq.id);
                if (!linkedBid.empty())
                { ImGui::TextColored(ImVec4(0.5f,0.8f,1.f,0.85f), "<>"); ImGui::SameLine(); }

                if (ImGui::Selectable(lbl.c_str(), sel))
                {
                    s_SelectedEquipId    = eq.id;
                    s_LastClickedIsBuild = false;
                    if (!linkedBid.empty()) s_SelectedBuildId = linkedBid;
                }
                ImGui::PopID();
            }
            ImGui::EndChild();

            // ── Builds column ─────────────────────────────────────────────────
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("Builds");
            ImGui::Separator();
            ImGui::BeginChild("##build_col", ImVec2(0, listH - 32.f), false,
                              ImGuiWindowFlags_None);
            if (builds.empty())
                ImGui::TextDisabled("  None. Use 'Import Builds'.");
            else for (auto& b : builds)
            {
                ImGui::PushID(b.id.c_str());
                bool sel = (b.id == s_SelectedBuildId);
                std::string lbl = b.label.empty() ? b.rawTab.buildName : b.label;
                if (lbl.empty()) lbl = "Build";

                std::string linkedEid = TemplateStore::GetLinkedEquip(b.id);
                float linkW = 0.f;
                if (!linkedEid.empty())
                {
                    ImGui::TextColored(ImVec4(0.5f,0.8f,1.f,0.85f), "<>");
                    ImGui::SameLine();
                    linkW = ImGui::CalcTextSize("<>").x + ImGui::GetStyle().ItemSpacing.x;
                }

                ImGui::TextColored(ProfessionColor(b.profession), "*");
                ImGui::SameLine();
                const float dotW   = ImGui::CalcTextSize("*").x + ImGui::GetStyle().ItemSpacing.x;
                const float copyW  = 46.f;
                const float avail  = ImGui::GetContentRegionAvail().x;
                const float nameW  = std::max(10.f, avail - linkW - dotW - copyW
                                              - ImGui::GetStyle().ItemSpacing.x);

                ImGui::PushClipRect(
                    ImGui::GetCursorScreenPos(),
                    ImVec2(ImGui::GetCursorScreenPos().x + nameW,
                           ImGui::GetCursorScreenPos().y +
                           ImGui::GetTextLineHeightWithSpacing()), true);
                if (ImGui::Selectable(lbl.c_str(), sel,
                                      ImGuiSelectableFlags_None, ImVec2(nameW, 0)))
                {
                    s_SelectedBuildId    = b.id;
                    s_LastClickedIsBuild = true;
                    if (!linkedEid.empty()) s_SelectedEquipId = linkedEid;
                }
                ImGui::PopClipRect();

                ImGui::SameLine(avail - copyW);
                bool hasCode = !b.chatCode.empty();
                if (!hasCode) ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                                                  ImGui::GetStyle().Alpha * 0.4f);
                if (ImGui::Button("Copy##bc") && hasCode)
                    ImGui::SetClipboardText(b.chatCode.c_str());
                if (!hasCode)
                {
                    ImGui::PopStyleVar();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Chat code pending resolution.");
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTable();
        }

        // ── Controls: Rename / Delete / Link / Unlink ─────────────────────────
        {
            std::string selId, selLabel;
            if (s_LastClickedIsBuild && !s_SelectedBuildId.empty())
                for (auto& b : builds)
                    if (b.id == s_SelectedBuildId)
                    { selId = b.id; selLabel = b.label.empty() ? b.rawTab.buildName : b.label; break; }
            if (!s_LastClickedIsBuild && !s_SelectedEquipId.empty())
                for (auto& eq : equips)
                    if (eq.id == s_SelectedEquipId)
                    { selId = eq.id; selLabel = eq.label.empty() ? eq.rawTab.tabName : eq.label; break; }

            if (!selId.empty())
            {
                ImGui::TextDisabled("%s", selLabel.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Rename##cr"))
                {
                    std::strncpy(s_RenameBuf, selLabel.c_str(), sizeof(s_RenameBuf)-1);
                    s_RenameTargetId = selId;
                    if (s_LastClickedIsBuild) s_ShowRenameBuild = true;
                    else                      s_ShowRenameEquip = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete##cd"))
                {
                    s_PendingDeleteId    = selId;
                    s_PendingDeleteLabel = selLabel;
                    if (s_LastClickedIsBuild) s_ShowDeleteBuildConfirm = true;
                    else                      s_ShowDeleteEquipConfirm = true;
                }
                ImGui::SameLine();
            }

            if (!s_SelectedBuildId.empty() && !s_SelectedEquipId.empty())
            {
                bool linked = (TemplateStore::GetLinkedEquip(s_SelectedBuildId) == s_SelectedEquipId);
                if (linked)
                {
                    ImGui::TextColored(ImVec4(0.5f,0.8f,1.f,1), "linked");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Unlink"))
                        TemplateStore::LinkBuildEquip(s_SelectedBuildId, "");
                }
                else
                {
                    if (ImGui::SmallButton("Link these"))
                        TemplateStore::LinkBuildEquip(s_SelectedBuildId, s_SelectedEquipId);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Associate the selected build with the selected equipment.");
                }
            }
        }

        ImGui::Separator();

        // ── Detail pane ───────────────────────────────────────────────────────
        ImGui::BeginChild("##detail_pane", ImVec2(0, 0), false);
        bool drew = false;

        // Always show what the user last clicked; if that item is linked, show
        // the combined view.  Only fall back to the other selection if nothing
        // was clicked yet (e.g. first frame after startup).
        if (s_LastClickedIsBuild && !s_SelectedBuildId.empty())
        {
            for (auto& b : builds) if (b.id == s_SelectedBuildId)
            {
                std::string eid = TemplateStore::GetLinkedEquip(b.id);
                if (!eid.empty())
                    for (auto& eq : equips) if (eq.id == eid)
                        { DrawCombinedDetail(b, eq); drew = true; break; }
                if (!drew) { DrawBuildDetail(b); drew = true; }
                break;
            }
        }
        else if (!s_LastClickedIsBuild && !s_SelectedEquipId.empty())
        {
            for (auto& eq : equips) if (eq.id == s_SelectedEquipId)
            {
                std::string bid = TemplateStore::GetLinkedBuild(eq.id);
                if (!bid.empty())
                    for (auto& b : builds) if (b.id == bid)
                        { DrawCombinedDetail(b, eq); drew = true; break; }
                if (!drew) { DrawEquipDetail(eq); drew = true; }
                break;
            }
        }

        if (!drew)
            ImGui::TextDisabled("Select a build or equipment set above to view details.");

        ImGui::EndChild();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ── Popup: Import Builds from GW2 ────────────────────────────────────────
    // ─────────────────────────────────────────────────────────────────────────

    void DrawImportBuildPopup()
    {
        if (!s_ShowImportBuild) return;

        ImGui::SetNextWindowSize(ImVec2(560, 450), ImGuiCond_FirstUseEver);
        bool open = true;
        if (ImGui::Begin("Import Builds from GW2##armoury_import_build", &open,
                         ImGuiWindowFlags_NoCollapse))
        {
            const std::string charName = ActiveChar();
            ImGui::TextDisabled("Character: %s", charName.c_str());
            ImGui::Separator();

            if (s_ImportPhase == ImportPhase::Fetching)
            {
                ImGui::TextUnformatted(s_ImportStatus.c_str());
            }
            else if (s_ImportPhase == ImportPhase::Error)
            {
                ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "%s", s_ImportStatus.c_str());
                if (ImGui::Button("Retry")) FetchBuildTabsAsync(charName);
            }
            else if (s_ImportPhase == ImportPhase::Ready)
            {
                // Import All — overwrites existing builds with the same name
                if (ImGui::Button("Import All  (overwrites same-named builds)##iab"))
                {
                    for (auto& tab : s_ImportedBuildTabs)
                    {
                        TemplateStore::StoredBuildTemplate tmpl;
                        tmpl.characterName = charName;
                        tmpl.profession    = tab.profession;
                        tmpl.rawTab        = tab;
                        tmpl.label         = tab.buildName.empty()
                                             ? "Tab " + std::to_string(tab.tabNumber)
                                             : tab.buildName;
                        tmpl.gw2BuildId    = MumbleLink ? (int)MumbleLink->Context.BuildId : 0;
                        s_SelectedBuildId  = TemplateStore::SaveBuildByLabel(std::move(tmpl));
                    }
                    TemplateStore::RequestResolve(g_Settings.ApiKey);
                    s_ShowImportBuild = false;
                }
                ImGui::Separator();

                for (auto& tab : s_ImportedBuildTabs)
                {
                    ImGui::PushID(tab.tabNumber);
                    std::string hdr = "Tab " + std::to_string(tab.tabNumber);
                    if (!tab.buildName.empty()) hdr += ": " + tab.buildName;
                    if (tab.isActive)           hdr += "  [Active]";
                    ImGui::TextColored(ProfessionColor(tab.profession), "%s", hdr.c_str());
                    ImGui::Indent(12.f);
                    for (auto& spec : tab.specs)
                        if (spec.id) ImGui::BulletText("%s", SpecDisplay(spec.id).c_str());
                    ImGui::Unindent(12.f);
                    if (ImGui::SmallButton("Import this tab##itb"))
                    {
                        TemplateStore::StoredBuildTemplate tmpl;
                        tmpl.characterName = charName;
                        tmpl.profession    = tab.profession;
                        tmpl.rawTab        = tab;
                        tmpl.label         = tab.buildName.empty()
                                             ? "Tab " + std::to_string(tab.tabNumber)
                                             : tab.buildName;
                        tmpl.gw2BuildId    = MumbleLink ? (int)MumbleLink->Context.BuildId : 0;
                        s_SelectedBuildId  = TemplateStore::SaveBuild(std::move(tmpl));
                        TemplateStore::RequestResolve(g_Settings.ApiKey);
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }
            }
        }
        ImGui::End();

        if (!open)
        {
            s_ShowImportBuild = false;
            s_ImportPhase     = ImportPhase::Idle;
            s_ImportedBuildTabs.clear();
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ── Popup: Import Equipment from GW2 ─────────────────────────────────────
    // ─────────────────────────────────────────────────────────────────────────

    void DrawImportEquipPopup()
    {
        if (!s_ShowImportEquip) return;

        ImGui::SetNextWindowSize(ImVec2(560, 450), ImGuiCond_FirstUseEver);
        bool open = true;
        if (ImGui::Begin("Import Equipment from GW2##armoury_import_equip", &open,
                         ImGuiWindowFlags_NoCollapse))
        {
            const std::string charName = ActiveChar();
            ImGui::TextDisabled("Character: %s", charName.c_str());
            ImGui::Separator();

            if (s_ImportPhase == ImportPhase::Fetching)
            {
                ImGui::TextUnformatted(s_ImportStatus.c_str());
            }
            else if (s_ImportPhase == ImportPhase::Error)
            {
                ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "%s", s_ImportStatus.c_str());
                if (ImGui::Button("Retry")) FetchEquipTabsAsync(charName);
            }
            else if (s_ImportPhase == ImportPhase::Ready)
            {
                if (ImGui::Button("Import All  (overwrites same-named equipment)##iae"))
                {
                    for (auto& tab : s_ImportedEquipTabs)
                    {
                        TemplateStore::StoredEquipmentTemplate tmpl;
                        tmpl.characterName = charName;
                        tmpl.rawTab        = tab;
                        tmpl.label         = tab.tabName.empty()
                                             ? "Tab " + std::to_string(tab.tabNumber)
                                             : tab.tabName;
                        s_SelectedEquipId  = TemplateStore::SaveEquipmentByLabel(std::move(tmpl));
                    }
                    TemplateStore::RequestResolve(g_Settings.ApiKey);
                    s_ShowImportEquip = false;
                }
                ImGui::Separator();

                for (auto& tab : s_ImportedEquipTabs)
                {
                    ImGui::PushID(tab.tabNumber);
                    std::string hdr = "Tab " + std::to_string(tab.tabNumber);
                    if (!tab.tabName.empty()) hdr += ": " + tab.tabName;
                    if (tab.isActive)         hdr += "  [Active]";
                    ImGui::TextUnformatted(hdr.c_str());
                    ImGui::TextDisabled("  %d pieces", (int)tab.pieces.size());
                    if (ImGui::SmallButton("Import this tab##ite"))
                    {
                        TemplateStore::StoredEquipmentTemplate tmpl;
                        tmpl.characterName = charName;
                        tmpl.rawTab        = tab;
                        tmpl.label         = tab.tabName.empty()
                                             ? "Tab " + std::to_string(tab.tabNumber)
                                             : tab.tabName;
                        s_SelectedEquipId  = TemplateStore::SaveEquipment(std::move(tmpl));
                        TemplateStore::RequestResolve(g_Settings.ApiKey);
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }
            }
        }
        ImGui::End();

        if (!open)
        {
            s_ShowImportEquip = false;
            s_ImportPhase     = ImportPhase::Idle;
            s_ImportedEquipTabs.clear();
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ── Popup: Import from code ───────────────────────────────────────────────
    // ─────────────────────────────────────────────────────────────────────────

    void DrawImportCodePopup()
    {
        if (!s_ShowImportCode) return;

        ImGui::SetNextWindowSize(ImVec2(500, 340), ImGuiCond_FirstUseEver);
        bool open = true;
        if (ImGui::Begin("Import Build from Code##armoury_import_code", &open,
                         ImGuiWindowFlags_NoCollapse))
        {
            ImGui::TextDisabled("Paste a build template chat link  e.g. [&DQE...]");
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::InputText("##code_in", s_CodeBuf, sizeof(s_CodeBuf)))
            {
                s_CodeParsed = false;
                s_CodeParseStatus.clear();
            }

            if (ImGui::Button("Parse##parsebtn"))
            {
                std::string prof;
                int    specIds[3]       = {};
                uint8_t choices[3][3]   = {};
                uint16_t palette[10]    = {};

                if (ChatCode::ParseBuildCode(s_CodeBuf, prof, specIds, choices, palette))
                {
                    s_ParsedProfession = prof;
                    for (int i=0;i<3;i++)  s_ParsedSpecIds[i]          = specIds[i];
                    for (int i=0;i<3;i++)  for (int j=0;j<3;j++) s_ParsedSpecChoices[i][j] = choices[i][j];
                    for (int i=0;i<10;i++) s_ParsedPaletteSkills[i]    = palette[i];
                    s_CodeParsed      = true;
                    s_CodeParseStatus = "Parsed OK \xe2\x80\x93 " + prof;
                    if (s_CodeLabelBuf[0] == '\0')
                        std::strncpy(s_CodeLabelBuf, prof.c_str(), sizeof(s_CodeLabelBuf)-1);
                }
                else
                {
                    s_CodeParsed      = false;
                    s_CodeParseStatus = "Not a valid build template code (must be type 0x0D).";
                }
            }

            if (!s_CodeParseStatus.empty())
            {
                ImVec4 col = s_CodeParsed
                    ? ImVec4(0.4f,1.f,0.4f,1) : ImVec4(1.f,0.4f,0.4f,1);
                ImGui::TextColored(col, "%s", s_CodeParseStatus.c_str());
            }

            if (s_CodeParsed)
            {
                ImGui::Separator();
                ImGui::TextDisabled("Profession: %s", s_ParsedProfession.c_str());
                for (int s=0;s<3;++s)
                {
                    if (!s_ParsedSpecIds[s]) continue;
                    ImGui::BulletText("Spec %d: %s  (choices %d/%d/%d)",
                        s+1, SpecDisplay(s_ParsedSpecIds[s]).c_str(),
                        s_ParsedSpecChoices[s][0],
                        s_ParsedSpecChoices[s][1],
                        s_ParsedSpecChoices[s][2]);
                }

                ImGui::Spacing();
                ImGui::Text("Save as:");
                ImGui::SetNextItemWidth(280.f);
                ImGui::InputText("##codelbl", s_CodeLabelBuf, sizeof(s_CodeLabelBuf));
                ImGui::SameLine();

                const std::string charName = ActiveChar();
                bool canSave = (s_CodeLabelBuf[0] != '\0') && !charName.empty();
                if (!canSave) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);

                if (ImGui::Button("Save##save_code") && canSave)
                {
                    TemplateStore::StoredBuildTemplate tmpl;
                    tmpl.characterName = charName;
                    tmpl.profession    = s_ParsedProfession;
                    tmpl.label         = s_CodeLabelBuf;
                    // Strip surrounding whitespace from code before storing
                    {
                        std::string raw = s_CodeBuf;
                        while (!raw.empty() && isspace((unsigned char)raw.front())) raw.erase(raw.begin());
                        while (!raw.empty() && isspace((unsigned char)raw.back()))  raw.pop_back();
                        tmpl.chatCode = raw;
                    }
                    tmpl.rawTab.profession = s_ParsedProfession;
                    for (int i=0;i<3;i++) tmpl.rawTab.specs[i].id = s_ParsedSpecIds[i];
                    for (int i=0;i<10;i++) tmpl.paletteSkills[i]  = s_ParsedPaletteSkills[i];
                    for (int i=0;i<3;i++) for (int j=0;j<3;j++) tmpl.specTraitChoices[i][j] = s_ParsedSpecChoices[i][j];
                    tmpl.gw2BuildId = MumbleLink ? (int)MumbleLink->Context.BuildId : 0;

                    s_SelectedBuildId = TemplateStore::SaveBuildByLabel(std::move(tmpl));
                    TemplateStore::RequestResolve(g_Settings.ApiKey);
                    s_ShowImportCode = false;
                }

                if (!canSave) ImGui::PopStyleVar();
                if (!canSave && charName.empty())
                    ImGui::TextDisabled("Select a character first.");
            }
        }
        ImGui::End();

        if (!open) s_ShowImportCode = false;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ── Modal: rename ─────────────────────────────────────────────────────────
    // ─────────────────────────────────────────────────────────────────────────

    void DrawRenameModal(bool forBuild)
    {
        const char* popupId = forBuild ? "Rename Build##armoury_ren_b"
                                       : "Rename Equipment##armoury_ren_e";
        bool& show = forBuild ? s_ShowRenameBuild : s_ShowRenameEquip;
        if (show) { ImGui::OpenPopup(popupId); show = false; }

        if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("New name:");
            ImGui::SetNextItemWidth(280.f);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            bool enter = ImGui::InputText("##rn_in", s_RenameBuf, sizeof(s_RenameBuf),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Spacing();
            if (ImGui::Button("OK", ImVec2(80,0)) || enter)
            {
                std::string n(s_RenameBuf);
                if (!n.empty())
                {
                    if (forBuild) TemplateStore::RenameBuild(s_RenameTargetId,     n);
                    else          TemplateStore::RenameEquipment(s_RenameTargetId, n);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80,0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ── Modal: delete confirmation ────────────────────────────────────────────
    // ─────────────────────────────────────────────────────────────────────────

    void DrawDeleteModal(bool forBuild)
    {
        const char* popupId = forBuild ? "Delete Build?##armoury_del_b"
                                       : "Delete Equipment?##armoury_del_e";
        bool& show = forBuild ? s_ShowDeleteBuildConfirm : s_ShowDeleteEquipConfirm;
        if (show) { ImGui::OpenPopup(popupId); show = false; }

        if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Delete \"%s\"?", s_PendingDeleteLabel.c_str());
            ImGui::TextDisabled("This cannot be undone.");
            ImGui::Spacing();
            if (ImGui::Button("Delete", ImVec2(80,0)))
            {
                if (forBuild)
                {
                    if (s_SelectedBuildId == s_PendingDeleteId) s_SelectedBuildId.clear();
                    TemplateStore::DeleteBuild(s_PendingDeleteId);
                }
                else
                {
                    if (s_SelectedEquipId == s_PendingDeleteId) s_SelectedEquipId.clear();
                    TemplateStore::DeleteEquipment(s_PendingDeleteId);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80,0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

} // anonymous namespace

// ── Public: Render ────────────────────────────────────────────────────────────

void UI::Render()
{
    // Drain any icons that finished downloading since the last frame.
    // Must run on the render thread before any Textures_GetOrCreateFromMemory calls.
    FlushPendingIcons();

    if (!g_Settings.ShowWindow) return;

    ImGui::SetNextWindowSize(
        ImVec2((float)g_Settings.WindowWidth, (float)g_Settings.WindowHeight),
        ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Armoury##armoury_main", &g_Settings.ShowWindow,
                     ImGuiWindowFlags_NoCollapse))
    {
        ImVec2 sz = ImGui::GetWindowSize();
        g_Settings.WindowWidth  = (int)sz.x;
        g_Settings.WindowHeight = (int)sz.y;

        if (g_Settings.ShowOnlyCurrentChar)
        {
            // No character panel — title shows current char from MumbleLink
            if (MumbleIdent && MumbleIdent->Name[0] != '\0')
                ImGui::TextDisabled("Character: %s", MumbleIdent->Name);
            else
                ImGui::TextDisabled("Not in-game / no character selected.");
            ImGui::Separator();
            DrawMainPanel();
        }
        else
        {
            const float kLeftWidth = 150.f;

            DrawCharacterPanel(kLeftWidth);
            ImGui::SameLine();

            // Vertical separator (manual — SeparatorEx is internal API)
            {
                ImVec2 p = ImGui::GetCursorScreenPos();
                float  h = ImGui::GetContentRegionAvail().y;
                ImGui::GetWindowDrawList()->AddLine(
                    p, ImVec2(p.x, p.y + h),
                    ImGui::GetColorU32(ImGuiCol_Separator));
                ImGui::Dummy(ImVec2(1.0f, h));
            }
            ImGui::SameLine();

            ImGui::BeginChild("##right_panel", ImVec2(0, 0), false);
            DrawMainPanel();
            ImGui::EndChild();
        }
    }
    ImGui::End();

    DrawImportBuildPopup();
    DrawImportEquipPopup();
    DrawImportCodePopup();

    DrawRenameModal(true);
    DrawRenameModal(false);
    DrawDeleteModal(true);
    DrawDeleteModal(false);
}

// ── Public: RenderOptions ─────────────────────────────────────────────────────

void UI::RenderOptions()
{
    // NOTE: Do NOT call ImGui::Begin/End here — Nexus calls this inside its own
    //       Options window.  Just write widgets directly.

    ImGui::TextDisabled("GW2 API Key");
    ImGui::TextDisabled("Requires 'characters' and 'builds' permissions.");

    static char s_KeyBuf[73] = {};
    static bool s_KeyBufInit = false;
    if (!s_KeyBufInit)
    {
        std::strncpy(s_KeyBuf, g_Settings.ApiKey.c_str(), sizeof(s_KeyBuf) - 1);
        s_KeyBufInit = true;
    }

    ImGui::SetNextItemWidth(340.f);
    if (ImGui::InputText("##api_key", s_KeyBuf, sizeof(s_KeyBuf),
                         ImGuiInputTextFlags_Password))
        g_Settings.ApiKey = s_KeyBuf;

    ImGui::SameLine();
    if (ImGui::SmallButton("Save & Verify"))
    {
        g_Settings.ApiKey = s_KeyBuf;
        g_Settings.Save();

        std::thread([]{
            auto status = GW2Api::ValidateKey(g_Settings.ApiKey);
            if (status == GW2Api::KeyStatus::Valid ||
                status == GW2Api::KeyStatus::NoPermissions)
            {
                FetchCharactersAsync();
            }
        }).detach();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("Display");
    if (ImGui::Checkbox("Show icons (fetches from GW2 render server)", &g_Settings.ShowIcons))
        g_Settings.Save();

    if (ImGui::Checkbox("Only show builds for the currently logged-in character",
                        &g_Settings.ShowOnlyCurrentChar))
        g_Settings.Save();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (!g_Settings.ApiKey.empty())
    {
        if (ImGui::Button("Refresh Character List"))
            FetchCharactersAsync();
    }
}
