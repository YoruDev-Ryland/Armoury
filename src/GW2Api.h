#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace GW2Api
{
    // ── Re-usable lookup info cached from the API ─────────────────────────────

    struct SpecInfo
    {
        int         id         = 0;
        std::string name;
        std::string profession; // e.g. "Guardian"
        bool        isElite    = false;
        std::string iconUrl;
        // Each trait "minor" slot is an ID from the traits array in the spec def.
        // The 9 selectable trait IDs are stored in major_traits, 3 per tier.
        std::vector<int> majorTraitIds; // 9 entries: tier0[0..2], tier1[3..5], tier2[6..8]
        std::vector<int> minorTraitIds; // 3 entries: tier0, tier1, tier2
    };

    struct TraitInfo
    {
        int         id = 0;
        std::string name;
        std::string iconUrl;
    };

    struct SkillInfo
    {
        int         id = 0;
        std::string name;
        std::string iconUrl;
        std::string chatLink;
    };

    struct ItemInfo
    {
        int         id             = 0;
        std::string name;
        std::string rarity;        // "Exotic", "Ascended", etc.
        std::string type;          // "Weapon", "Armor", "UpgradeComponent", etc.
        std::string iconUrl;
        std::string chatLink;
        int         infixUpgradeId = 0; // details.infix_upgrade.id for items with fixed stats
    };

    struct StatsInfo
    {
        int         id   = 0;
        std::string name; // "Berserker's", "Viper's", etc.
    };

    // ── API key validation ────────────────────────────────────────────────────
    enum class KeyStatus
    {
        Unknown,
        Valid,
        Invalid,
        NoPermissions, // key exists but missing "characters" or "builds" scopes
    };

    // ── Build template raw data from /v2/characters/{name}/buildtabs ─────────

    struct TraitSelection
    {
        int id        = 0;        // specialization definition ID
        int traits[3] = {0,0,0}; // selected trait IDs per tier (0 = none selected)
    };

    struct SkillBar
    {
        int heal            = 0;
        int utilities[3]    = {0,0,0};
        int elite           = 0;
        // Revenant legends — nullptr / 0 for non-revenant professions
        int legend1         = 0;
        int legend2         = 0;
        // Ranger pets
        int terrestrialPet1 = 0;
        int terrestrialPet2 = 0;
        int aquaticPet1     = 0;
        int aquaticPet2     = 0;
    };

    struct BuildTab
    {
        int         tabNumber  = 0;
        bool        isActive   = false;
        std::string buildName;          // in-game template name
        std::string profession;         // "Guardian", "Warrior", etc.
        TraitSelection specs[3];        // up to 3 specialization lines
        SkillBar       skills;
        // Aquatic skill bar (optional)
        SkillBar       aquaticSkills;
    };

    // ── Equipment template raw data from /v2/characters/{name}/equipmenttabs ─

    struct EquipmentPiece
    {
        int              itemId    = 0;
        std::string      slot;          // "Head", "Shoulders", "Chest", ...
        int              skinId    = 0;
        std::vector<int> upgradeIds;    // runes, sigils
        std::vector<int> infusionIds;
        int              statsId   = 0;
        std::string      statsName;     // resolved from API
        // Binding: "Account" or "Character" or ""
        std::string      binding;
    };

    struct EquipmentTab
    {
        int                        tabNumber = 0;
        bool                       isActive  = false;
        std::string                tabName;
        std::vector<EquipmentPiece> pieces;
    };

    // ── Public API ─────────────────────────────────────────────────────────────

    // Validate the API key.  Blocking — call from a background thread.
    KeyStatus ValidateKey(const std::string& apiKey);

    // Return all character names accessible by the key.  Blocking.
    std::vector<std::string> FetchCharacters(const std::string& apiKey);

    // Return all build tabs for a character.  Blocking.
    // Requires "builds" + "characters" permissions on the key.
    std::vector<BuildTab> FetchBuildTabs(const std::string& apiKey,
                                         const std::string& characterName);

    // Return all equipment tabs for a character.  Blocking.
    std::vector<EquipmentTab> FetchEquipmentTabs(const std::string& apiKey,
                                                  const std::string& characterName);

    // Fetch specialization definitions for a list of IDs.
    std::vector<SpecInfo>  FetchSpecInfos(const std::vector<int>& ids);

    // Fetch skill definitions for a list of IDs.
    std::vector<SkillInfo> FetchSkillInfos(const std::vector<int>& ids);

    // Fetch item definitions for a list of IDs (max 200 per call).
    std::vector<ItemInfo>  FetchItemInfos(const std::vector<int>& ids);

    // Fetch item stat names by ID.
    std::vector<StatsInfo> FetchStatsInfos(const std::vector<int>& ids);

    // Fetch trait definitions for a list of IDs.
    // Traits are selected within specializations; resolved via /v2/traits.
    std::vector<TraitInfo> FetchTraitInfos(const std::vector<int>& ids);

    // Fetch the skill-palette mapping for a profession.
    // Returns map of palette_id -> api_skill_id.
    // Requires ?v=latest on the endpoint (no API key needed).
    std::unordered_map<int,int> FetchProfessionPalette(const std::string& profession);

    // Return the current GW2 game build number from /v2/build.  0 on failure.
    int FetchCurrentBuildId();

    // Download raw bytes from any HTTPS URL (e.g. render CDN icons).
    // Returns empty vector on failure.  Blocking — call from a background thread.
    std::vector<uint8_t> DownloadBytesFromURL(const std::string& url);
}
