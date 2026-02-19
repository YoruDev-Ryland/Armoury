#include "UI.h"
#include "Settings.h"
#include "TemplateStore.h"
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

// ── Name resolution helpers ───────────────────────────────────────────────────

// Returns a human-readable name for a skill/spec ID, with fallback to "ID #nnn"
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

// ── Profession colour map ─────────────────────────────────────────────────────

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

// ── UI state ──────────────────────────────────────────────────────────────────

namespace
{
    // Character list
    std::vector<std::string>  s_Characters;
    std::atomic_bool          s_CharactersFetching{false};
    std::string               s_CharactersStatus;   // "" or error message

    std::string               s_SelectedChar;
    int                       s_RightTab = 0;  // 0=Builds 1=Equipment

    // Selected template in the list
    std::string               s_SelectedBuildId;
    std::string               s_SelectedEquipId;

    // Delete confirmation
    bool                      s_ShowDeleteBuildConfirm = false;
    bool                      s_ShowDeleteEquipConfirm = false;
    std::string               s_PendingDeleteId;

    // Rename state
    bool                      s_ShowRenameBuild = false;
    bool                      s_ShowRenameEquip = false;
    std::string               s_RenameTargetId;
    char                      s_RenameBuf[128] = {};

    // Import popup state
    enum class ImportPhase { Idle, Fetching, Ready, Error };

    bool                               s_ShowImportBuild = false;
    bool                               s_ShowImportEquip = false;
    ImportPhase                        s_ImportPhase  = ImportPhase::Idle;
    std::string                        s_ImportStatus;
    std::vector<GW2Api::BuildTab>      s_ImportedBuildTabs;
    std::vector<GW2Api::EquipmentTab>  s_ImportedEquipTabs;

    // Slot display order for equipment
    constexpr const char* kEquipSlots[] = {
        "Head", "Shoulders", "Chest", "Hands", "Legs", "Feet",
        "Back", "Accessory1", "Accessory2", "Amulet", "Ring1", "Ring2",
        "WeaponA1", "WeaponA2", "WeaponB1", "WeaponB2",
        "Sickle", "Axe", "Pick"  // gathering tools
    };

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
            if (tabs.empty())
            {
                s_ImportStatus = "No build tabs returned. Check your API key has 'builds' permission.";
                s_ImportPhase  = ImportPhase::Error;
            }
            else
            {
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
            if (tabs.empty())
            {
                s_ImportStatus = "No equipment tabs returned. Check API key permissions.";
                s_ImportPhase  = ImportPhase::Error;
            }
            else
            {
                s_ImportedEquipTabs = std::move(tabs);
                s_ImportPhase       = ImportPhase::Ready;
                s_ImportStatus.clear();
                TemplateStore::RequestResolve(g_Settings.ApiKey);
            }
        }).detach();
    }

    // ── Popup: Import Build Tabs ──────────────────────────────────────────────

    void DrawImportBuildPopup()
    {
        if (!s_ShowImportBuild) return;

        ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
        bool open = true;
        if (ImGui::Begin("Import Build Tabs##armoury_import_build", &open,
                         ImGuiWindowFlags_NoCollapse))
        {
            ImGui::TextDisabled("Character: %s", s_SelectedChar.c_str());
            ImGui::Separator();

            if (s_ImportPhase == ImportPhase::Fetching)
            {
                ImGui::TextUnformatted(s_ImportStatus.c_str());
            }
            else if (s_ImportPhase == ImportPhase::Error)
            {
                ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "%s", s_ImportStatus.c_str());
                if (ImGui::Button("Retry"))
                    FetchBuildTabsAsync(s_SelectedChar);
            }
            else if (s_ImportPhase == ImportPhase::Ready)
            {
                for (auto& tab : s_ImportedBuildTabs)
                {
                    ImGui::PushID(tab.tabNumber);

                    std::string header = "Tab " + std::to_string(tab.tabNumber);
                    if (!tab.buildName.empty()) header += ": " + tab.buildName;
                    if (tab.isActive) header += "  [Active]";

                    ImGui::TextColored(ProfessionColor(tab.profession),
                                       "%s", header.c_str());

                    // Show spec lines
                    ImGui::Indent(12.f);
                    for (auto& spec : tab.specs)
                    {
                        if (!spec.id) continue;
                        std::string sn = SpecDisplay(spec.id);
                        ImGui::BulletText("%s", sn.c_str());
                    }
                    ImGui::Unindent(12.f);

                    if (ImGui::SmallButton("Import this tab"))
                    {
                        TemplateStore::StoredBuildTemplate tmpl;
                        tmpl.characterName = s_SelectedChar;
                        tmpl.profession    = tab.profession;
                        tmpl.rawTab        = tab;
                        std::string label  = tab.buildName.empty()
                                             ? "Tab " + std::to_string(tab.tabNumber)
                                             : tab.buildName;
                        tmpl.label         = label;
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
            s_ShowImportBuild  = false;
            s_ImportPhase      = ImportPhase::Idle;
            s_ImportedBuildTabs.clear();
        }
    }

    // ── Popup: Import Equipment Tabs ──────────────────────────────────────────

    void DrawImportEquipPopup()
    {
        if (!s_ShowImportEquip) return;

        ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
        bool open = true;
        if (ImGui::Begin("Import Equipment Tabs##armoury_import_equip", &open,
                         ImGuiWindowFlags_NoCollapse))
        {
            ImGui::TextDisabled("Character: %s", s_SelectedChar.c_str());
            ImGui::Separator();

            if (s_ImportPhase == ImportPhase::Fetching)
            {
                ImGui::TextUnformatted(s_ImportStatus.c_str());
            }
            else if (s_ImportPhase == ImportPhase::Error)
            {
                ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "%s", s_ImportStatus.c_str());
                if (ImGui::Button("Retry"))
                    FetchEquipTabsAsync(s_SelectedChar);
            }
            else if (s_ImportPhase == ImportPhase::Ready)
            {
                for (auto& tab : s_ImportedEquipTabs)
                {
                    ImGui::PushID(tab.tabNumber);

                    std::string header = "Tab " + std::to_string(tab.tabNumber);
                    if (!tab.tabName.empty()) header += ": " + tab.tabName;
                    if (tab.isActive) header += "  [Active]";

                    ImGui::TextUnformatted(header.c_str());
                    ImGui::TextDisabled("  %d pieces", (int)tab.pieces.size());

                    if (ImGui::SmallButton("Import this tab"))
                    {
                        TemplateStore::StoredEquipmentTemplate tmpl;
                        tmpl.characterName = s_SelectedChar;
                        tmpl.rawTab        = tab;
                        std::string label  = tab.tabName.empty()
                                             ? "Tab " + std::to_string(tab.tabNumber)
                                             : tab.tabName;
                        tmpl.label         = label;
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
            s_ShowImportEquip  = false;
            s_ImportPhase      = ImportPhase::Idle;
            s_ImportedEquipTabs.clear();
        }
    }

    // ── Build template detail view ────────────────────────────────────────────

    void DrawBuildDetail(const TemplateStore::StoredBuildTemplate& b)
    {
        ImGui::Separator();
        ImGui::TextColored(ProfessionColor(b.profession),
                           "%s", b.profession.c_str());

        auto& tab = b.rawTab;

        // Specializations
        ImGui::Text("Specializations");
        ImGui::Indent(12.f);
        for (auto& spec : tab.specs)
        {
            if (!spec.id) continue;
            std::string sn = SpecDisplay(spec.id);
            ImGui::BulletText("%s", sn.c_str());
            // Traits inline (small, comma-separated)
            std::string traitStr;
            for (int ti = 0; ti < 3; ++ti)
            {
                if (!spec.traits[ti]) continue;
                if (!traitStr.empty()) traitStr += ", ";
                traitStr += TraitDisplay(spec.traits[ti]);
            }
            if (!traitStr.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("  %s", traitStr.c_str());
            }
        }
        ImGui::Unindent(12.f);

        ImGui::Spacing();

        // Skills
        ImGui::Text("Skills");
        ImGui::Indent(12.f);
        ImGui::BulletText("Heal:    %s", SkillDisplay(tab.skills.heal).c_str());
        for (int ui = 0; ui < 3; ++ui)
            ImGui::BulletText("Utility: %s", SkillDisplay(tab.skills.utilities[ui]).c_str());
        ImGui::BulletText("Elite:   %s", SkillDisplay(tab.skills.elite).c_str());
        ImGui::Unindent(12.f);

        // Ranger pets
        if (tab.skills.terrestrialPet1 || tab.skills.terrestrialPet2)
        {
            ImGui::Spacing();
            ImGui::Text("Pets");
            ImGui::Indent(12.f);
            if (tab.skills.terrestrialPet1)
                ImGui::BulletText("Pet 1: #%d", tab.skills.terrestrialPet1);
            if (tab.skills.terrestrialPet2)
                ImGui::BulletText("Pet 2: #%d", tab.skills.terrestrialPet2);
            ImGui::Unindent(12.f);
        }
    }

    // ── Equipment template detail view ────────────────────────────────────────

    void DrawEquipDetail(const TemplateStore::StoredEquipmentTemplate& e)
    {
        ImGui::Separator();

        auto& tab = e.rawTab;

        // Show pieces in a table
        if (ImGui::BeginTable("equip_detail", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
            ImVec2(0, 260)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Slot",      ImGuiTableColumnFlags_WidthFixed, 100.f);
            ImGui::TableSetupColumn("Item",      ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Stats / Upgrades", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            // Display in canonical slot order
            for (auto& slotName : kEquipSlots)
            {
                // Find piece for this slot
                const GW2Api::EquipmentPiece* piece = nullptr;
                for (auto& p : tab.pieces)
                    if (p.slot == slotName) { piece = &p; break; }

                if (!piece && tab.pieces.size() > 0) continue; // skip empty slots

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(slotName);

                if (!piece)
                {
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled("(empty)");
                    ImGui::TableSetColumnIndex(2);
                    continue;
                }

                ImGui::TableSetColumnIndex(1);
                std::string itemName = ItemDisplay(piece->itemId);
                std::string rarity   = TemplateStore::GetItemRarity(piece->itemId);
                if (!rarity.empty())
                    ImGui::TextColored(RarityColorV4(rarity), "%s", itemName.c_str());
                else
                    ImGui::TextUnformatted(itemName.c_str());

                ImGui::TableSetColumnIndex(2);
                std::string statsStr;
                if (!piece->statsName.empty()) statsStr = piece->statsName;

                for (int uid : piece->upgradeIds)
                {
                    if (!statsStr.empty()) statsStr += ", ";
                    statsStr += ItemDisplay(uid);
                }
                if (statsStr.empty() && piece->infusionIds.empty())
                    ImGui::TextDisabled("-");
                else
                    ImGui::TextUnformatted(statsStr.c_str());
            }

            // Show any pieces that didn't match the canonical list
            for (auto& p : tab.pieces)
            {
                bool inList = false;
                for (auto& s : kEquipSlots) if (p.slot == s) { inList = true; break; }
                if (inList) continue;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(p.slot.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(ItemDisplay(p.itemId).c_str());
                ImGui::TableSetColumnIndex(2);
                if (!p.statsName.empty()) ImGui::TextUnformatted(p.statsName.c_str());
            }

            ImGui::EndTable();
        }
    }

    // ── Left panel: character list ────────────────────────────────────────────

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
                bool selected = (name == s_SelectedChar);
                if (ImGui::Selectable(name.c_str(), selected,
                                      ImGuiSelectableFlags_None,
                                      ImVec2(panelWidth - 8.f, 0)))
                {
                    s_SelectedChar    = name;
                    s_SelectedBuildId.clear();
                    s_SelectedEquipId.clear();
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
        {
            if (ImGui::SmallButton("Refresh"))
                FetchCharactersAsync();
        }

        ImGui::EndChild();
    }

    // ── Right panel: builds ───────────────────────────────────────────────────

    void DrawBuildsPanel()
    {
        // Toolbar
        if (!s_SelectedChar.empty())
        {
            if (ImGui::Button("Import from GW2##build"))
            {
                s_ShowImportBuild = true;
                s_ImportPhase     = ImportPhase::Idle;
                s_ImportedBuildTabs.clear();
                FetchBuildTabsAsync(s_SelectedChar);
            }
        }

        ImGui::Separator();

        if (s_SelectedChar.empty())
        {
            ImGui::TextDisabled("Select a character from the list.");
            return;
        }

        auto builds = TemplateStore::GetBuilds(s_SelectedChar);

        if (builds.empty())
        {
            ImGui::TextDisabled("No saved builds for %s.\nUse 'Import from GW2' to add one.",
                                s_SelectedChar.c_str());
            return;
        }

        // Template list
        ImGui::BeginChild("##build_list", ImVec2(0, 180), false,
                          ImGuiWindowFlags_HorizontalScrollbar);

        for (auto& b : builds)
        {
            ImGui::PushID(b.id.c_str());

            bool sel = (b.id == s_SelectedBuildId);
            std::string label = b.label.empty() ? b.rawTab.buildName : b.label;
            if (label.empty()) label = "Build";

            // Profession coloured dot
            ImGui::TextColored(ProfessionColor(b.profession), "●");
            ImGui::SameLine();

            if (ImGui::Selectable(label.c_str(), sel,
                                  ImGuiSelectableFlags_AllowDoubleClick))
                s_SelectedBuildId = b.id;

            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100.f);
            if (ImGui::SmallButton("Rename"))
            {
                s_ShowRenameBuild   = true;
                s_RenameTargetId    = b.id;
                std::strncpy(s_RenameBuf, label.c_str(), sizeof(s_RenameBuf)-1);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete"))
            {
                s_ShowDeleteBuildConfirm = true;
                s_PendingDeleteId        = b.id;
            }

            ImGui::PopID();
        }

        ImGui::EndChild();

        // Detail view for selected build
        if (!s_SelectedBuildId.empty())
        {
            for (auto& b : builds)
            {
                if (b.id == s_SelectedBuildId)
                {
                    DrawBuildDetail(b);
                    break;
                }
            }
        }
    }

    // ── Right panel: equipment ────────────────────────────────────────────────

    void DrawEquipmentPanel()
    {
        if (!s_SelectedChar.empty())
        {
            if (ImGui::Button("Import from GW2##equip"))
            {
                s_ShowImportEquip = true;
                s_ImportPhase     = ImportPhase::Idle;
                s_ImportedEquipTabs.clear();
                FetchEquipTabsAsync(s_SelectedChar);
            }
        }

        ImGui::Separator();

        if (s_SelectedChar.empty())
        {
            ImGui::TextDisabled("Select a character from the list.");
            return;
        }

        auto equipment = TemplateStore::GetEquipment(s_SelectedChar);

        if (equipment.empty())
        {
            ImGui::TextDisabled("No saved equipment sets for %s.\nUse 'Import from GW2' to add one.",
                                s_SelectedChar.c_str());
            return;
        }

        ImGui::BeginChild("##equip_list", ImVec2(0, 100), false,
                          ImGuiWindowFlags_HorizontalScrollbar);

        for (auto& e : equipment)
        {
            ImGui::PushID(e.id.c_str());

            bool sel = (e.id == s_SelectedEquipId);
            std::string label = e.label.empty() ? e.rawTab.tabName : e.label;
            if (label.empty()) label = "Equipment";

            if (ImGui::Selectable(label.c_str(), sel))
                s_SelectedEquipId = e.id;

            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100.f);
            if (ImGui::SmallButton("Rename"))
            {
                s_ShowRenameEquip = true;
                s_RenameTargetId  = e.id;
                std::strncpy(s_RenameBuf, label.c_str(), sizeof(s_RenameBuf)-1);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete"))
            {
                s_ShowDeleteEquipConfirm = true;
                s_PendingDeleteId        = e.id;
            }

            ImGui::PopID();
        }

        ImGui::EndChild();

        if (!s_SelectedEquipId.empty())
        {
            for (auto& e : equipment)
            {
                if (e.id == s_SelectedEquipId)
                {
                    DrawEquipDetail(e);
                    break;
                }
            }
        }
    }

    // ── Modal: rename ─────────────────────────────────────────────────────────

    void DrawRenameModal(bool forBuild)
    {
        const char* popupId = forBuild ? "Rename Build##armoury_ren_b"
                                       : "Rename Equipment##armoury_ren_e";
        bool& show = forBuild ? s_ShowRenameBuild : s_ShowRenameEquip;

        if (show)
        {
            ImGui::OpenPopup(popupId);
            show = false;
        }

        if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("New name:");
            ImGui::SetNextItemWidth(280.f);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            bool enter = ImGui::InputText("##rename_input", s_RenameBuf,
                                          sizeof(s_RenameBuf),
                                          ImGuiInputTextFlags_EnterReturnsTrue);

            ImGui::Spacing();
            if (ImGui::Button("OK", ImVec2(80,0)) || enter)
            {
                std::string newName(s_RenameBuf);
                if (!newName.empty())
                {
                    if (forBuild) TemplateStore::RenameBuild(s_RenameTargetId,     newName);
                    else          TemplateStore::RenameEquipment(s_RenameTargetId, newName);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80,0)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    // ── Modal: delete confirmation ────────────────────────────────────────────

    void DrawDeleteModal(bool forBuild)
    {
        const char* popupId = forBuild ? "Delete Build?##armoury_del_b"
                                       : "Delete Equipment?##armoury_del_e";
        bool& show = forBuild ? s_ShowDeleteBuildConfirm : s_ShowDeleteEquipConfirm;

        if (show)
        {
            ImGui::OpenPopup(popupId);
            show = false;
        }

        if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Delete this template? This cannot be undone.");
            ImGui::Spacing();

            if (ImGui::Button("Delete", ImVec2(80, 0)))
            {
                if (forBuild)
                {
                    if (s_SelectedBuildId == s_PendingDeleteId)
                        s_SelectedBuildId.clear();
                    TemplateStore::DeleteBuild(s_PendingDeleteId);
                }
                else
                {
                    if (s_SelectedEquipId == s_PendingDeleteId)
                        s_SelectedEquipId.clear();
                    TemplateStore::DeleteEquipment(s_PendingDeleteId);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80,0)))
                ImGui::CloseCurrentPopup();

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
        // Save window size on change
        ImVec2 sz = ImGui::GetWindowSize();
        g_Settings.WindowWidth  = (int)sz.x;
        g_Settings.WindowHeight = (int)sz.y;

        const float kLeftWidth = 150.f;

        // ── Left panel ────────────────────────────────────────────────────────
        DrawCharacterPanel(kLeftWidth);
        ImGui::SameLine();

        // ── Vertical separator ────────────────────────────────────────────────
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float  h = ImGui::GetContentRegionAvail().y;
            ImGui::GetWindowDrawList()->AddLine(
                p, ImVec2(p.x, p.y + h),
                ImGui::GetColorU32(ImGuiCol_Separator));
            ImGui::Dummy(ImVec2(1.0f, h));
        }
        ImGui::SameLine();

        // ── Right panel ───────────────────────────────────────────────────────
        ImGui::BeginChild("##right_panel", ImVec2(0, 0), false);

        // Tab bar: Builds | Equipment
        if (ImGui::BeginTabBar("##armoury_tabs"))
        {
            if (ImGui::BeginTabItem("Builds"))
            {
                s_RightTab = 0;
                DrawBuildsPanel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Equipment"))
            {
                s_RightTab = 1;
                DrawEquipmentPanel();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::EndChild();
    }
    ImGui::End();

    // ── Overlaid popups and modals ────────────────────────────────────────────
    DrawImportBuildPopup();
    DrawImportEquipPopup();

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
    {
        g_Settings.ApiKey = s_KeyBuf;
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Save & Verify"))
    {
        g_Settings.ApiKey = s_KeyBuf;
        g_Settings.Save();

        // Validate in background and refresh characters on success
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

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Populate character list trigger
    if (!g_Settings.ApiKey.empty())
    {
        if (ImGui::Button("Refresh Character List"))
            FetchCharactersAsync();
    }
}
