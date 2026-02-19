#pragma once
#include "GW2Api.h"
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace TemplateStore
{
    // ── Stored build template ─────────────────────────────────────────────────

    struct StoredBuildTemplate
    {
        std::string id;           // unique key: "<charname>_build_<timestamp>"
        std::string label;        // user-editable display name
        std::string characterName;
        std::string profession;   // "Guardian", "Warrior", etc.

        // Raw tab data (preserved exactly from API so nothing is lost)
        GW2Api::BuildTab rawTab;

        // Human-readable resolved names (populated asynchronously by Resolve*)
        // Maps spec ID -> spec name (e.g. 27 -> "Dragonhunter")
        std::unordered_map<int, std::string> specNames;
        // Maps skill ID -> skill name
        std::unordered_map<int, std::string> skillNames;
        // Maps trait ID -> trait name
        std::unordered_map<int, std::string> traitNames;

        // ── Chat-code data ────────────────────────────────────────────────────
        // Ready-to-copy "[&DQ...]" string; empty until generated.
        std::string chatCode;
        // Raw palette skill IDs (same ordering as ChatCode::PaletteSlot).
        uint16_t paletteSkills[10] = {};
        // Trait choice positions [spec_slot][tier] (1=top,2=mid,3=bot,0=none).
        uint8_t specTraitChoices[3][3] = {};
        // Ranger pet IDs [terr1,terr2,aq1,aq2]
        uint8_t rangerPets[4] = {};
        // Revenant legend codes [t1,t2,aq1,aq2] and inactive palette skills [6]
        uint8_t  revenantLegends[4]  = {};
        uint16_t revenantInactive[6] = {};

        int64_t savedAt = 0;      // Unix timestamp (seconds)

        // True once all IDs have been resolved via background thread
        bool resolved = false;
    };

    // ── Stored equipment template ─────────────────────────────────────────────

    struct StoredEquipmentTemplate
    {
        std::string id;           // unique key: "<charname>_equip_<timestamp>"
        std::string label;        // user-editable display name
        std::string characterName;
        std::string profession;

        // Raw tab data
        GW2Api::EquipmentTab rawTab;

        // Resolved item names: item ID -> display name
        std::unordered_map<int, std::string> itemNames;
        // Resolved item rarities: item ID -> rarity string
        std::unordered_map<int, std::string> itemRarities;
        // Resolved upgrade/infusion names: item ID -> name
        std::unordered_map<int, std::string> upgradeNames;

        int64_t savedAt = 0;
        bool    resolved = false;
    };

    // ── Store management ──────────────────────────────────────────────────────

    // Initialise — must be called after APIDefs is set.
    void Init();

    // Shutdown — stops background resolve thread, saves to disk.
    void Shutdown();

    // Load store from <addondir>/armoury.json.
    void Load();

    // Save store to disk.
    void Save();

    // ── Build templates ───────────────────────────────────────────────────────

    // Return a copy of all stored build templates (thread-safe).
    std::vector<StoredBuildTemplate> GetAllBuilds();

    // Return builds for a specific character.
    std::vector<StoredBuildTemplate> GetBuilds(const std::string& characterName);

    // Add (or replace) a build template.  Returns the generated ID.
    std::string SaveBuild(StoredBuildTemplate tmpl);

    // Delete a build by ID.
    void DeleteBuild(const std::string& id);

    // Update just the label of a build.
    void RenameBuild(const std::string& id, const std::string& newLabel);

    // Save a build, replacing any existing build with the same label for
    // the same character (used for "Import All" to avoid duplicates).
    std::string SaveBuildByLabel(StoredBuildTemplate tmpl);

    // ── Equipment templates ───────────────────────────────────────────────────

    std::vector<StoredEquipmentTemplate> GetAllEquipment();
    std::vector<StoredEquipmentTemplate> GetEquipment(const std::string& characterName);

    std::string SaveEquipment(StoredEquipmentTemplate tmpl);
    void DeleteEquipment(const std::string& id);
    void RenameEquipment(const std::string& id, const std::string& newLabel);

    // Save equipment, replacing any with the same label for the same character.
    std::string SaveEquipmentByLabel(StoredEquipmentTemplate tmpl);

    // ── API resolution cache ──────────────────────────────────────────────────
    // These are saved to disk alongside templates so names survive restarts.

    // Look up a cached spec name by ID.  Returns "" if not in cache.
    std::string GetSpecName(int id);

    // Look up a cached skill name by ID.
    std::string GetSkillName(int id);

    // Look up a cached item name by ID.
    std::string GetItemName(int id);

    // Look up a cached item rarity by ID.
    std::string GetItemRarity(int id);

    // Look up a cached trait name by ID (traits are part of specializations).
    std::string GetTraitName(int id);

    // Look up major traits for a spec by ID.
    // Returns empty array if not yet cached.
    std::array<int,9> GetSpecMajorTraits(int specId);

    // Palette-to-API skill ID lookup for a profession.
    // Returns 0 if not cached or not found.
    int PaletteToApi(const std::string& profession, int paletteId);

    // API-skill-ID to palette ID lookup for a profession.
    // Returns 0 if not cached or not found.
    int ApiToPalette(const std::string& profession, int apiSkillId);

    // Queue IDs for background resolution (starts resolve thread if needed).
    void RequestResolve(const std::string& apiKey);

    // Mutex for thread-safe access to store vectors from UI thread.
    extern std::mutex g_Mutex;
}
