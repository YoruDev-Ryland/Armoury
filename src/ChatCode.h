#pragma once
#include "GW2Api.h"
#include <string>
#include <array>
#include <unordered_map>
#include <cstdint>

namespace ChatCode
{
    std::string           B64Encode(const std::vector<uint8_t>& data);
    bool                  B64Decode(const std::string& in, std::vector<uint8_t>& out);
    uint8_t     ProfCode(const std::string& profession);
    std::string ProfName(uint8_t code);

    enum PaletteSlot
    {
        PS_TerrHeal=0, PS_AqHeal, PS_TerrUtil1, PS_AqUtil1,
        PS_TerrUtil2,  PS_AqUtil2, PS_TerrUtil3, PS_AqUtil3,
        PS_TerrElite,  PS_AqElite
    };

    bool ParseBuildCode(const std::string& code,
                        std::string& outProfession,
                        int          outSpecIds[3],
                        uint8_t      outSpecChoices[3][3],
                        uint16_t     outPaletteSkills[10]);

    std::string EncodeBuildCode(
        const std::string& profession,
        const int          specIds[3],
        const uint8_t      specChoices[3][3],
        const uint16_t     paletteSkills[10],
        const uint8_t*     rangerPets          = nullptr,
        const uint8_t*     revenantLegends     = nullptr,
        const uint16_t*    revenantInactive    = nullptr);

    void DeriveChoices(const GW2Api::BuildTab& tab,
                       const std::unordered_map<int, std::array<int,9>>& specMajorTraits,
                       uint8_t outChoices[3][3]);

    void ApplyChoices(GW2Api::BuildTab& tab,
                      const std::unordered_map<int, std::array<int,9>>& specMajorTraits,
                      const uint8_t choices[3][3]);
}
