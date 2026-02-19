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
    if (rarity == "Legendary")   return IM_COL32( 76,  19, 157, 255);
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
    bool IsArmorSlot(const std::string& slot)
    {
        return slot=="Head"||slot=="Shoulders"||slot=="Chest"||
               slot=="Hands"||slot=="Legs"||slot=="Feet";
    }
    bool IsWeaponSlot(const std::string& slot)
    {
        return slot=="WeaponA1"||slot=="WeaponA2"||
               slot=="WeaponB1"||slot=="WeaponB2";
    }

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

        // Profession + read-only code line
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

        ImGui::Separator();

        // Specializations + traits
        ImGui::TextDisabled("Specializations");
        for (int si = 0; si < 3; ++si)
        {
            auto& spec = tab.specs[si];
            if (!spec.id) continue;

            ImGui::BulletText("%s", SpecDisplay(spec.id).c_str());
            ImGui::Indent(16.f);
            for (int tier = 0; tier < 3; ++tier)
            {
                if (!spec.traits[tier]) continue;
                ImGui::TextDisabled("Tier %d: %s", tier+1,
                                    TraitDisplay(spec.traits[tier]).c_str());
            }
            ImGui::Unindent(16.f);
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Skills");
        ImGui::Indent(12.f);
        ImGui::BulletText("Heal:    %s", SkillDisplay(tab.skills.heal).c_str());
        for (int ui = 0; ui < 3; ++ui)
            ImGui::BulletText("Utility: %s", SkillDisplay(tab.skills.utilities[ui]).c_str());
        ImGui::BulletText("Elite:   %s", SkillDisplay(tab.skills.elite).c_str());
        ImGui::Unindent(12.f);

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

        // ── Armour & Accessories ──────────────────────────────────────────────
        ImGui::TextDisabled("Armour & Accessories");
        if (ImGui::BeginTable("equip_armor", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
            ImVec2(0, 180)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Slot",        ImGuiTableColumnFlags_WidthFixed, 90.f);
            ImGui::TableSetupColumn("Item",        ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Rune / Stats",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            constexpr const char* kArmorOrder[] = {
                "Head","Shoulders","Chest","Hands","Legs","Feet","Back",
                "Accessory1","Accessory2","Amulet","Ring1","Ring2"
            };

            for (auto& slotName : kArmorOrder)
            {
                const GW2Api::EquipmentPiece* piece = nullptr;
                for (auto& p : tab.pieces) if (p.slot==slotName){piece=&p;break;}
                if (!piece) continue;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(slotName);

                ImGui::TableSetColumnIndex(1);
                {
                    std::string rarity = TemplateStore::GetItemRarity(piece->itemId);
                    if (!rarity.empty())
                        ImGui::TextColored(RarityColorV4(rarity), "%s",
                                           ItemDisplay(piece->itemId).c_str());
                    else
                        ImGui::TextUnformatted(ItemDisplay(piece->itemId).c_str());
                }

                ImGui::TableSetColumnIndex(2);
                {
                    std::string col3;
                    if (!piece->statsName.empty()) col3 = piece->statsName;
                    for (int uid : piece->upgradeIds)
                    {
                        if (!col3.empty()) col3 += "\n";
                        std::string uname = ItemDisplay(uid);
                        col3 += IsArmorSlot(slotName) ? "Rune: " + uname : uname;
                    }
                    if (col3.empty()) ImGui::TextDisabled("-");
                    else              ImGui::TextUnformatted(col3.c_str());
                }
            }
            ImGui::EndTable();
        }

        // ── Weapons ───────────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::TextDisabled("Weapons");
        if (ImGui::BeginTable("equip_weapons", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Slot",          ImGuiTableColumnFlags_WidthFixed, 90.f);
            ImGui::TableSetupColumn("Item",          ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Sigil / Stats", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            constexpr const char* kWeapOrder[] = {
                "WeaponA1","WeaponA2","WeaponB1","WeaponB2"
            };

            bool anyWeapon = false;
            for (auto& slotName : kWeapOrder)
            {
                const GW2Api::EquipmentPiece* piece = nullptr;
                for (auto& p : tab.pieces) if (p.slot==slotName){piece=&p;break;}
                if (!piece) continue;
                anyWeapon = true;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(slotName);

                ImGui::TableSetColumnIndex(1);
                {
                    std::string rarity = TemplateStore::GetItemRarity(piece->itemId);
                    if (!rarity.empty())
                        ImGui::TextColored(RarityColorV4(rarity), "%s",
                                           ItemDisplay(piece->itemId).c_str());
                    else
                        ImGui::TextUnformatted(ItemDisplay(piece->itemId).c_str());
                }

                ImGui::TableSetColumnIndex(2);
                {
                    std::string col3;
                    if (!piece->statsName.empty()) col3 = piece->statsName;
                    for (int uid : piece->upgradeIds)
                    {
                        if (!col3.empty()) col3 += "\n";
                        col3 += "Sigil: " + ItemDisplay(uid);
                    }
                    if (col3.empty()) ImGui::TextDisabled("-");
                    else              ImGui::TextUnformatted(col3.c_str());
                }
            }
            if (!anyWeapon)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("(none)");
            }
            ImGui::EndTable();
        }
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

        // ── Combined list (no horizontal scrollbar) ───────────────────────────
        auto builds = TemplateStore::GetBuilds(charName);
        auto equips = TemplateStore::GetEquipment(charName);

        ImGui::BeginChild("##combined_list", ImVec2(0, 200.f), true,
                          ImGuiWindowFlags_None);

        // — Builds section —
        ImGui::TextDisabled("Builds");
        ImGui::Separator();

        if (builds.empty())
        {
            ImGui::TextDisabled("  No saved builds. Use 'Import Builds'.");
        }
        else
        {
            for (auto& b : builds)
            {
                ImGui::PushID(b.id.c_str());

                bool sel = (b.id == s_SelectedBuildId);
                std::string label = b.label.empty() ? b.rawTab.buildName : b.label;
                if (label.empty()) label = "Build";

                const float copyW  = 50.f;
                const float dotW   = ImGui::CalcTextSize("●").x + ImGui::GetStyle().ItemSpacing.x;
                const float avail  = ImGui::GetContentRegionAvail().x;
                const float nameW  = avail - dotW - copyW - ImGui::GetStyle().ItemSpacing.x * 2.f;

                // Profession-coloured dot
                ImGui::TextColored(ProfessionColor(b.profession), "●");
                ImGui::SameLine();

                // Selectable name — clipped to avoid causing horizontal scroll
                ImGui::PushClipRect(
                    ImGui::GetCursorScreenPos(),
                    ImVec2(ImGui::GetCursorScreenPos().x + nameW,
                           ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeightWithSpacing()),
                    true);
                if (ImGui::Selectable(label.c_str(), sel,
                                      ImGuiSelectableFlags_None,
                                      ImVec2(nameW, 0)))
                {
                    s_SelectedBuildId = b.id;
                    s_SelectedEquipId.clear();
                }
                ImGui::PopClipRect();

                // Copy button — right-aligned on same row
                ImGui::SameLine(avail - copyW);
                bool hasCode = !b.chatCode.empty();
                if (!hasCode) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);
                if (ImGui::Button("Copy##bc") && hasCode)
                    ImGui::SetClipboardText(b.chatCode.c_str());
                if (!hasCode)
                {
                    ImGui::PopStyleVar();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Chat code not yet generated.\nWait for background resolution to complete.");
                }

                ImGui::PopID();
            }
        }

        ImGui::Spacing();

        // — Equipment section —
        ImGui::TextDisabled("Equipment");
        ImGui::Separator();

        if (equips.empty())
        {
            ImGui::TextDisabled("  No saved equipment. Use 'Import Equipment'.");
        }
        else
        {
            for (auto& e : equips)
            {
                ImGui::PushID(e.id.c_str());

                bool sel = (e.id == s_SelectedEquipId);
                std::string label = e.label.empty() ? e.rawTab.tabName : e.label;
                if (label.empty()) label = "Equipment";

                if (ImGui::Selectable(label.c_str(), sel))
                {
                    s_SelectedEquipId = e.id;
                    s_SelectedBuildId.clear();
                }

                ImGui::PopID();
            }
        }

        ImGui::EndChild();

        // ── Edit controls (below list — safe from accidental clicks) ──────────
        std::string selLabel;
        bool isBuildSel = false, isEquipSel = false;

        if (!s_SelectedBuildId.empty())
        {
            for (auto& b : builds)
                if (b.id == s_SelectedBuildId)
                {
                    selLabel  = b.label.empty() ? b.rawTab.buildName : b.label;
                    isBuildSel = true;
                    break;
                }
        }
        else if (!s_SelectedEquipId.empty())
        {
            for (auto& eq : equips)
                if (eq.id == s_SelectedEquipId)
                {
                    selLabel  = eq.label.empty() ? eq.rawTab.tabName : eq.label;
                    isEquipSel = true;
                    break;
                }
        }

        if (isBuildSel || isEquipSel)
        {
            ImGui::TextDisabled("Selected: %s", selLabel.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Rename##selrn"))
            {
                std::strncpy(s_RenameBuf, selLabel.c_str(), sizeof(s_RenameBuf)-1);
                s_RenameTargetId = isBuildSel ? s_SelectedBuildId : s_SelectedEquipId;
                if (isBuildSel) s_ShowRenameBuild = true;
                else            s_ShowRenameEquip = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete##seldel"))
            {
                s_PendingDeleteId    = isBuildSel ? s_SelectedBuildId : s_SelectedEquipId;
                s_PendingDeleteLabel = selLabel;
                if (isBuildSel) s_ShowDeleteBuildConfirm = true;
                else            s_ShowDeleteEquipConfirm = true;
            }
        }

        ImGui::Separator();

        // ── Detail view ───────────────────────────────────────────────────────
        ImGui::BeginChild("##detail_pane", ImVec2(0, 0), false);

        if (!s_SelectedBuildId.empty())
        {
            for (auto& b : builds)
                if (b.id == s_SelectedBuildId) { DrawBuildDetail(b); break; }
        }
        else if (!s_SelectedEquipId.empty())
        {
            for (auto& eq : equips)
                if (eq.id == s_SelectedEquipId) { DrawEquipDetail(eq); break; }
        }
        else
        {
            ImGui::TextDisabled("Select a build or equipment set above to view details.");
        }

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
