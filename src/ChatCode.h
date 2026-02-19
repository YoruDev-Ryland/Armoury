#pragma once
#include "GW2Api.h"
#include <string>
#include <array>
#include <unordered_map>
#include <cstdint>

// ── GW2 build template chat-link encoder / decoder ───────────────────────────
//
// Format (type 0x0D), all values little-endian:
//   [0]      0x0D  (build template type)
//   [1]      profession code (1=Guardian … 9=Revenant)
//   [2,3]    spec0 id + trait-choice byte
//   [4,5]    spec1 id + trait-choice byte
//   [6,7]    spec2 id + trait-choice byte
//   [8..27]  10 × uint16 palette skill IDs
//              (terr_heal, aq_heal, terr_u1, aq_u1, terr_u2, aq_u2,
//               terr_u3, aq_u3, terr_elite, aq_elite)
//   [28..43] 16 bytes profession-specific (Ranger-pets / Revenant-legends)
//   [44]     weapon-count  (SotO extension; we write 0)
//   [45]     skill-variant-count (SotO ext; we write 0)
//
// Trait-choice byte:  bits 5-4 = tier-2 choice,  3-2 = tier-1,  1-0 = tier-0
//                     choice values:  0=none  1=top  2=mid  3=bot
//
// Skills are stored as palette IDs, NOT GW2 API skill IDs.
// Use TemplateStore::PaletteToApi / ApiToPalette to convert.

namespace ChatCode
{
    // ── Base64 ────────────────────────────────────────────────────────────────

    std::string           B64Encode(const std::vector<uint8_t>& data);
    bool                  B64Decode(const std::string& in, std::vector<uint8_t>& out);

    // ── Profession code ───────────────────────────────────────────────────────

    uint8_t     ProfCode(const std::string& profession); // "Guardian" -> 1
    std::string ProfName(uint8_t code);                  // 1 -> "Guardian"

    // Palette-skill index ordering (0-9)
    enum PaletteSlot
    {
        PS_TerrHeal=0, PS_AqHeal, PS_TerrUtil1, PS_AqUtil1,
        PS_TerrUtil2,  PS_AqUtil2, PS_TerrUtil3, PS_AqUtil3,
        PS_TerrElite,  PS_AqElite
    };

    // ── Parse ─────────────────────────────────────────────────────────────────

    // Accepts "[&DQE...]" or "DQE..." (with/without brackets).
    // Fills:
    //   outProfession     – "Guardian", "Warrior", etc.
    //   outSpecIds[3]     – specialisation API IDs (0 = empty slot)
    //   outSpecChoices[3][3] – choice positions [spec_slot][tier] (1/2/3/0=none)
    //   outPaletteSkills[10] – raw palette IDs
    // Returns false if the code is not a valid 0x0D build code.
    bool ParseBuildCode(const std::string& code,
                        std::string& outProfession,
                        int          outSpecIds[3],
                        uint8_t      outSpecChoices[3][3],
                        uint16_t     outPaletteSkills[10]);

    // ── Encode ────────────────────────────────────────────────────────────────

    // Produce a "[&DQ...]" chat-link string.
    // paletteSkills[10] must contain palette IDs (0 = empty slot).
    // specChoices[3][3]: choice position per-spec per-tier (1/2/3, 0=none).
    // For Rangers supply rangerPets[4] = {terr1,terr2,aq1,aq2} (bytes).
    // For Revenants supply revenantLegends[4] + revenantInactiveSkills[6] (uint16).
    std::string EncodeBuildCode(
        const std::string& profession,
        const int          specIds[3],
        const uint8_t      specChoices[3][3],
        const uint16_t     paletteSkills[10],
        const uint8_t*     rangerPets          = nullptr,   // [4] or null
        const uint8_t*     revenantLegends     = nullptr,   // [4] or null
        const uint16_t*    revenantInactive    = nullptr);  // [6] or null

    // ── Trait-choice helpers ──────────────────────────────────────────────────

    // Convert API trait IDs stored in tab.specs[s].traits[t] → choice positions,
    // using the 9 major-trait IDs per spec (from /v2/specializations major_traits).
    // specMajorTraits[spec_id] must have exactly 9 entries:
    //   indices [0..2] = tier-0 (top/mid/bot), [3..5] = tier-1, [6..8] = tier-2.
    // Output: outChoices[3][3]  [spec_slot][tier] = 1/2/3/0.
    void DeriveChoices(const GW2Api::BuildTab& tab,
                       const std::unordered_map<int, std::array<int,9>>& specMajorTraits,
                       uint8_t outChoices[3][3]);

    // Reverse: fill tab.specs[s].traits[t] from choice positions.
    void ApplyChoices(GW2Api::BuildTab& tab,
                      const std::unordered_map<int, std::array<int,9>>& specMajorTraits,
                      const uint8_t choices[3][3]);
}
