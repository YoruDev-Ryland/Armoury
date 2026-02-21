#include "ChatCode.h"
#include <cstring>
#include <algorithm>

static const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return 0;
}

std::string ChatCode::B64Encode(const std::vector<uint8_t>& data)
{
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    for (size_t i = 0; i < data.size(); i += 3)
    {
        unsigned v  = data[i] << 16;
        if (i+1 < data.size()) v |= data[i+1] << 8;
        if (i+2 < data.size()) v |= data[i+2];

        out += kB64[(v >> 18) & 0x3F];
        out += kB64[(v >> 12) & 0x3F];
        out += (i+1 < data.size()) ? kB64[(v >>  6) & 0x3F] : '=';
        out += (i+2 < data.size()) ? kB64[(v      ) & 0x3F] : '=';
    }
    return out;
}

bool ChatCode::B64Decode(const std::string& in, std::vector<uint8_t>& out)
{
    out.clear();

    std::string s;
    s.reserve(in.size());
    for (char c : in) if (!isspace((unsigned char)c)) s += c;

    if (s.size() % 4 != 0) return false;

    out.reserve(s.size() / 4 * 3);
    for (size_t i = 0; i < s.size(); i += 4)
    {
        int a = b64val(s[i]),   b = b64val(s[i+1]);
        int c = b64val(s[i+2]), d = b64val(s[i+3]);
        unsigned v = (a << 18) | (b << 12) | (c << 6) | d;
        out.push_back((v >> 16) & 0xFF);
        if (s[i+2] != '=') out.push_back((v >>  8) & 0xFF);
        if (s[i+3] != '=') out.push_back(v & 0xFF);
    }
    return true;
}

uint8_t ChatCode::ProfCode(const std::string& p)
{
    if (p == "Guardian")    return 1;
    if (p == "Warrior")     return 2;
    if (p == "Engineer")    return 3;
    if (p == "Ranger")      return 4;
    if (p == "Thief")       return 5;
    if (p == "Elementalist")return 6;
    if (p == "Mesmer")      return 7;
    if (p == "Necromancer") return 8;
    if (p == "Revenant")    return 9;
    return 0;
}

std::string ChatCode::ProfName(uint8_t code)
{
    switch (code)
    {
        case 1: return "Guardian";
        case 2: return "Warrior";
        case 3: return "Engineer";
        case 4: return "Ranger";
        case 5: return "Thief";
        case 6: return "Elementalist";
        case 7: return "Mesmer";
        case 8: return "Necromancer";
        case 9: return "Revenant";
        default: return "";
    }
}

bool ChatCode::ParseBuildCode(const std::string& raw,
                               std::string& outProfession,
                               int          outSpecIds[3],
                               uint8_t      outSpecChoices[3][3],
                               uint16_t     outPaletteSkills[10])
{
    std::string s = raw;
    {
        auto lb = s.find("[&");
        if (lb != std::string::npos)
        {
            s = s.substr(lb + 2);
            auto rb = s.find(']');
            if (rb != std::string::npos) s = s.substr(0, rb);
        }
    }

    while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && isspace((unsigned char)s.back()))  s.pop_back();

    std::vector<uint8_t> bytes;
    if (!B64Decode(s, bytes)) return false;

    if (bytes.size() < 44 || bytes[0] != 0x0D) return false;

    outProfession = ProfName(bytes[1]);
    if (outProfession.empty()) return false;

    for (int s = 0; s < 3; ++s)
    {
        outSpecIds[s] = bytes[2 + s*2];
        uint8_t tb    = bytes[3 + s*2];
        outSpecChoices[s][0] = (tb     ) & 0x03;
        outSpecChoices[s][1] = (tb >> 2) & 0x03;
        outSpecChoices[s][2] = (tb >> 4) & 0x03;
    }

    for (int i = 0; i < 10; ++i)
    {
        uint16_t lo = bytes[8  + i*2];
        uint16_t hi = bytes[9  + i*2];
        outPaletteSkills[i] = (uint16_t)(lo | (hi << 8));
    }

    return true;
}

std::string ChatCode::EncodeBuildCode(
    const std::string& profession,
    const int          specIds[3],
    const uint8_t      specChoices[3][3],
    const uint16_t     paletteSkills[10],
    const uint8_t*     rangerPets,
    const uint8_t*     revenantLegends,
    const uint16_t*    revenantInactive)
{
    std::vector<uint8_t> bytes(46, 0);

    bytes[0] = 0x0D;
    bytes[1] = ProfCode(profession);

    for (int s = 0; s < 3; ++s)
    {
        bytes[2 + s*2] = (uint8_t)(specIds[s] & 0xFF);
        uint8_t tb = 0;
        tb |= (specChoices[s][0] & 0x03);
        tb |= (specChoices[s][1] & 0x03) << 2;
        tb |= (specChoices[s][2] & 0x03) << 4;
        bytes[3 + s*2] = tb;
    }

    for (int i = 0; i < 10; ++i)
    {
        bytes[8  + i*2] = (uint8_t)(paletteSkills[i] & 0xFF);
        bytes[9  + i*2] = (uint8_t)((paletteSkills[i] >> 8) & 0xFF);
    }

    if (profession == "Ranger" && rangerPets)
    {
        bytes[28] = rangerPets[0];
        bytes[29] = rangerPets[1];
        bytes[30] = rangerPets[2];
        bytes[31] = rangerPets[3];
    }
    else if (profession == "Revenant" && revenantLegends)
    {
        bytes[28] = revenantLegends[0];
        bytes[29] = revenantLegends[1];
        bytes[30] = revenantLegends[2];
        bytes[31] = revenantLegends[3];
        if (revenantInactive)
        {
            for (int i = 0; i < 6; ++i)
            {
                bytes[32 + i*2] = (uint8_t)(revenantInactive[i] & 0xFF);
                bytes[33 + i*2] = (uint8_t)((revenantInactive[i] >> 8) & 0xFF);
            }
        }
    }
    return "[&" + B64Encode(bytes) + "]";
}

void ChatCode::DeriveChoices(
    const GW2Api::BuildTab& tab,
    const std::unordered_map<int, std::array<int,9>>& specMajorTraits,
    uint8_t outChoices[3][3])
{
    for (int s = 0; s < 3; ++s)
    {
        for (int t = 0; t < 3; ++t) outChoices[s][t] = 0;

        int specId = tab.specs[s].id;
        if (!specId) continue;

        auto it = specMajorTraits.find(specId);
        if (it == specMajorTraits.end()) continue;

        const auto& maj = it->second;

        for (int tier = 0; tier < 3; ++tier)
        {
            int traitId = tab.specs[s].traits[tier];
            if (!traitId) continue;

            for (int pos = 0; pos < 3; ++pos)
            {
                if (maj[tier*3 + pos] == traitId)
                {
                    outChoices[s][tier] = (uint8_t)(pos + 1);
                    break;
                }
            }
        }
    }
}

void ChatCode::ApplyChoices(
    GW2Api::BuildTab& tab,
    const std::unordered_map<int, std::array<int,9>>& specMajorTraits,
    const uint8_t choices[3][3])
{
    for (int s = 0; s < 3; ++s)
    {
        for (int tier = 0; tier < 3; ++tier)
            tab.specs[s].traits[tier] = 0;

        int specId = tab.specs[s].id;
        if (!specId) continue;

        auto it = specMajorTraits.find(specId);
        if (it == specMajorTraits.end()) continue;

        const auto& maj = it->second;
        for (int tier = 0; tier < 3; ++tier)
        {
            uint8_t choice = choices[s][tier];
            if (choice < 1 || choice > 3) continue;
            int idx = tier*3 + (choice - 1);
            if (idx < 9) tab.specs[s].traits[tier] = maj[idx];
        }
    }
}
