#include "GW2Api.h"

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <sstream>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ── Internal HTTP helpers ─────────────────────────────────────────────────────

// Perform a synchronous HTTPS GET to api.guildwars2.com.
// path      — e.g. L"/v2/characters"
// apiKey    — if non-empty, added as "Authorization: Bearer <key>" header
// Returns the response body as UTF-8, or empty string on error.
static std::string ApiGet(const std::wstring& path,
                           const std::string&  apiKey = "")
{
    std::string result;

    HINTERNET hSession = WinHttpOpen(
        L"Armoury/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return result;

    HINTERNET hConnect = WinHttpConnect(
        hSession, L"api.guildwars2.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

    HINTERNET hReq = WinHttpOpenRequest(
        hConnect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hReq)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // Attach API key if provided
    if (!apiKey.empty())
    {
        std::wstring auth = L"Authorization: Bearer " +
            std::wstring(apiKey.begin(), apiKey.end());
        WinHttpAddRequestHeaders(hReq, auth.c_str(), (ULONG)-1L,
                                 WINHTTP_ADDREQ_FLAG_ADD);
    }

    if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, nullptr))
    {
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(hReq,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &status, &statusSize, nullptr);

        if (status == 200)
        {
            DWORD bytesAvail = 0;
            while (WinHttpQueryDataAvailable(hReq, &bytesAvail) && bytesAvail > 0)
            {
                std::string buf(bytesAvail, '\0');
                DWORD bytesRead = 0;
                WinHttpReadData(hReq, buf.data(), bytesAvail, &bytesRead);
                result.append(buf.data(), bytesRead);
            }
        }
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

// URL-encode a UTF-8 string for use in a path segment (replaces spaces etc.)
static std::wstring UrlEncode(const std::string& s)
{
    std::wstring out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out += static_cast<wchar_t>(c);
        }
        else
        {
            wchar_t buf[8];
            swprintf(buf, 8, L"%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// Build a comma-separated ID list path like L"/v2/items?ids=1,2,3"
static std::wstring IdsPath(const wchar_t* base, const std::vector<int>& ids)
{
    std::wstring path = base;
    path += L"?ids=";
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (i) path += L',';
        path += std::to_wstring(ids[i]);
    }
    return path;
}

// ── Safe JSON getters ─────────────────────────────────────────────────────────

static std::string jstr(const json& j, const char* key,
                        const std::string& def = "")
{
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return def;
}

static int jint(const json& j, const char* key, int def = 0)
{
    if (j.contains(key) && j[key].is_number()) return j[key].get<int>();
    return def;
}

static bool jbool(const json& j, const char* key, bool def = false)
{
    if (j.contains(key) && j[key].is_boolean()) return j[key].get<bool>();
    return def;
}

// ── Parse helpers ─────────────────────────────────────────────────────────────

static GW2Api::SkillBar ParseSkillBar(const json& j)
{
    GW2Api::SkillBar bar{};
    if (!j.is_object()) return bar;

    bar.heal = jint(j, "heal");
    bar.elite = jint(j, "elite");

    if (j.contains("utilities") && j["utilities"].is_array())
    {
        auto& arr = j["utilities"];
        for (int i = 0; i < 3 && i < (int)arr.size(); ++i)
            bar.utilities[i] = arr[i].is_number() ? arr[i].get<int>() : 0;
    }

    // Revenant legends
    if (j.contains("legends") && j["legends"].is_array())
    {
        auto& legs = j["legends"];
        // legends is an array of strings like ["Legend1","Legend2"] — we store
        // the raw string hash as a placeholder; UI can resolve to display name.
        // For simplicity we store 0 (unresolved) here; TemplateStore stores the
        // raw legend ID strings separately in StoredBuildTemplate.
    }

    // Ranger pets
    if (j.contains("pets") && j["pets"].is_object())
    {
        auto& pets = j["pets"];
        if (pets.contains("terrestrial") && pets["terrestrial"].is_array())
        {
            auto& terr = pets["terrestrial"];
            if (terr.size() > 0 && terr[0].is_number()) bar.terrestrialPet1 = terr[0].get<int>();
            if (terr.size() > 1 && terr[1].is_number()) bar.terrestrialPet2 = terr[1].get<int>();
        }
        if (pets.contains("aquatic") && pets["aquatic"].is_array())
        {
            auto& aq = pets["aquatic"];
            if (aq.size() > 0 && aq[0].is_number()) bar.aquaticPet1 = aq[0].get<int>();
            if (aq.size() > 1 && aq[1].is_number()) bar.aquaticPet2 = aq[1].get<int>();
        }
    }

    return bar;
}

// ── Public implementations ────────────────────────────────────────────────────

GW2Api::KeyStatus GW2Api::ValidateKey(const std::string& apiKey)
{
    if (apiKey.empty()) return KeyStatus::Invalid;

    std::string resp = ApiGet(L"/v2/tokeninfo", apiKey);
    if (resp.empty()) return KeyStatus::Invalid;

    try
    {
        json j = json::parse(resp);
        if (j.contains("text")) return KeyStatus::Invalid; // error response

        bool hasCharacters = false;
        bool hasBuilds     = false;

        if (j.contains("permissions") && j["permissions"].is_array())
        {
            for (auto& p : j["permissions"])
            {
                if (p.is_string())
                {
                    std::string perm = p.get<std::string>();
                    if (perm == "characters") hasCharacters = true;
                    if (perm == "builds")     hasBuilds     = true;
                }
            }
        }

        if (!hasCharacters || !hasBuilds)
            return KeyStatus::NoPermissions;

        return KeyStatus::Valid;
    }
    catch (...) {}
    return KeyStatus::Invalid;
}

std::vector<std::string> GW2Api::FetchCharacters(const std::string& apiKey)
{
    std::vector<std::string> out;
    std::string resp = ApiGet(L"/v2/characters", apiKey);
    if (resp.empty()) return out;

    try
    {
        json j = json::parse(resp);
        if (j.is_array())
            for (auto& v : j)
                if (v.is_string()) out.push_back(v.get<std::string>());
    }
    catch (...) {}
    return out;
}

std::vector<GW2Api::BuildTab> GW2Api::FetchBuildTabs(const std::string& apiKey,
                                                       const std::string& characterName)
{
    std::vector<BuildTab> out;
    std::wstring path = L"/v2/characters/" + UrlEncode(characterName) +
                        L"/buildtabs?tabs=all";
    std::string resp = ApiGet(path, apiKey);
    if (resp.empty()) return out;

    try
    {
        json arr = json::parse(resp);
        if (!arr.is_array()) return out;

        for (auto& jt : arr)
        {
            BuildTab tab;
            tab.tabNumber = jint(jt, "tab");
            tab.isActive  = jbool(jt, "is_active");

            if (!jt.contains("build") || !jt["build"].is_object()) continue;
            auto& jb = jt["build"];

            tab.buildName  = jstr(jb, "name");
            tab.profession = jstr(jb, "profession");

            // Specializations (up to 3 lines)
            if (jb.contains("specializations") && jb["specializations"].is_array())
            {
                int idx = 0;
                for (auto& js : jb["specializations"])
                {
                    if (idx >= 3) break;
                    if (!js.is_object()) { ++idx; continue; }
                    tab.specs[idx].id = jint(js, "id");
                    if (js.contains("traits") && js["traits"].is_array())
                    {
                        auto& jtr = js["traits"];
                        for (int t = 0; t < 3 && t < (int)jtr.size(); ++t)
                            tab.specs[idx].traits[t] = jtr[t].is_number()
                                                        ? jtr[t].get<int>() : 0;
                    }
                    ++idx;
                }
            }

            // Terrestrial skills
            if (jb.contains("skills") && jb["skills"].is_object())
                tab.skills = ParseSkillBar(jb["skills"]);

            // Aquatic skills
            if (jb.contains("aquatic_skills") && jb["aquatic_skills"].is_object())
                tab.aquaticSkills = ParseSkillBar(jb["aquatic_skills"]);

            out.push_back(std::move(tab));
        }
    }
    catch (...) {}
    return out;
}

std::vector<GW2Api::EquipmentTab> GW2Api::FetchEquipmentTabs(
    const std::string& apiKey,
    const std::string& characterName)
{
    std::vector<EquipmentTab> out;
    std::wstring path = L"/v2/characters/" + UrlEncode(characterName) +
                        L"/equipmenttabs?tabs=all";
    std::string resp = ApiGet(path, apiKey);
    if (resp.empty()) return out;

    try
    {
        json arr = json::parse(resp);
        if (!arr.is_array()) return out;

        for (auto& jt : arr)
        {
            EquipmentTab tab;
            tab.tabNumber = jint(jt, "tab");
            tab.isActive  = jbool(jt, "is_active");
            tab.tabName   = jstr(jt, "name");

            if (!jt.contains("equipment") || !jt["equipment"].is_array()) continue;

            for (auto& je : jt["equipment"])
            {
                if (!je.is_object()) continue;
                EquipmentPiece piece;
                piece.itemId  = jint(je, "id");
                piece.slot    = jstr(je, "slot");
                piece.skinId  = jint(je, "skin");
                piece.binding = jstr(je, "binding");

                if (je.contains("upgrades") && je["upgrades"].is_array())
                    for (auto& u : je["upgrades"])
                        if (u.is_number()) piece.upgradeIds.push_back(u.get<int>());

                if (je.contains("infusions") && je["infusions"].is_array())
                    for (auto& inf : je["infusions"])
                        if (inf.is_number()) piece.infusionIds.push_back(inf.get<int>());

                if (je.contains("stats") && je["stats"].is_object())
                {
                    piece.statsId   = jint(je["stats"], "id");
                    piece.statsName = jstr(je["stats"], "name");
                }

                tab.pieces.push_back(std::move(piece));
            }

            out.push_back(std::move(tab));
        }
    }
    catch (...) {}
    return out;
}

std::vector<GW2Api::SpecInfo> GW2Api::FetchSpecInfos(const std::vector<int>& ids)
{
    std::vector<SpecInfo> out;
    if (ids.empty()) return out;

    std::string resp = ApiGet(IdsPath(L"/v2/specializations", ids));
    if (resp.empty()) return out;

    try
    {
        json arr = json::parse(resp);
        if (!arr.is_array()) return out;

        for (auto& js : arr)
        {
            SpecInfo s;
            s.id         = jint(js,  "id");
            s.name       = jstr(js,  "name");
            s.profession = jstr(js,  "profession");
            s.isElite    = jbool(js, "elite");
            s.iconUrl    = jstr(js,  "icon");

            if (js.contains("major_traits") && js["major_traits"].is_array())
                for (auto& t : js["major_traits"])
                    if (t.is_number()) s.majorTraitIds.push_back(t.get<int>());

            out.push_back(std::move(s));
        }
    }
    catch (...) {}
    return out;
}

std::vector<GW2Api::SkillInfo> GW2Api::FetchSkillInfos(const std::vector<int>& ids)
{
    std::vector<SkillInfo> out;
    if (ids.empty()) return out;

    std::string resp = ApiGet(IdsPath(L"/v2/skills", ids));
    if (resp.empty()) return out;

    try
    {
        json arr = json::parse(resp);
        if (!arr.is_array()) return out;

        for (auto& js : arr)
        {
            SkillInfo s;
            s.id       = jint(js, "id");
            s.name     = jstr(js, "name");
            s.iconUrl  = jstr(js, "icon");
            s.chatLink = jstr(js, "chat_link");
            out.push_back(std::move(s));
        }
    }
    catch (...) {}
    return out;
}

std::vector<GW2Api::ItemInfo> GW2Api::FetchItemInfos(const std::vector<int>& ids)
{
    std::vector<ItemInfo> out;
    if (ids.empty()) return out;

    // Batch max 200 per call
    for (size_t start = 0; start < ids.size(); start += 200)
    {
        std::vector<int> batch(ids.begin() + start,
                               ids.begin() + std::min(start + 200, ids.size()));
        std::string resp = ApiGet(IdsPath(L"/v2/items", batch));
        if (resp.empty()) continue;

        try
        {
            json arr = json::parse(resp);
            if (!arr.is_array()) continue;

            for (auto& ji : arr)
            {
                ItemInfo item;
                item.id       = jint(ji, "id");
                item.name     = jstr(ji, "name");
                item.rarity   = jstr(ji, "rarity");
                item.type     = jstr(ji, "type");
                item.chatLink = jstr(ji, "chat_link");
                item.iconUrl  = jstr(ji, "icon");
                out.push_back(std::move(item));
            }
        }
        catch (...) {}
    }
    return out;
}

std::vector<GW2Api::StatsInfo> GW2Api::FetchStatsInfos(const std::vector<int>& ids)
{
    std::vector<StatsInfo> out;
    if (ids.empty()) return out;

    std::string resp = ApiGet(IdsPath(L"/v2/itemstats", ids));
    if (resp.empty()) return out;

    try
    {
        json arr = json::parse(resp);
        if (!arr.is_array()) return out;

        for (auto& js : arr)
        {
            StatsInfo s;
            s.id   = jint(js, "id");
            s.name = jstr(js, "name");
            out.push_back(s);
        }
    }
    catch (...) {}
    return out;
}

std::vector<GW2Api::TraitInfo> GW2Api::FetchTraitInfos(const std::vector<int>& ids)
{
    std::vector<TraitInfo> out;
    if (ids.empty()) return out;

    // Batch max 200 per call
    for (size_t start = 0; start < ids.size(); start += 200)
    {
        std::vector<int> batch(ids.begin() + start,
                               ids.begin() + std::min(start + 200, ids.size()));
        std::string resp = ApiGet(IdsPath(L"/v2/traits", batch));
        if (resp.empty()) continue;

        try
        {
            json arr = json::parse(resp);
            if (!arr.is_array()) continue;

            for (auto& jt : arr)
            {
                TraitInfo t;
                t.id      = jint(jt, "id");
                t.name    = jstr(jt, "name");
                t.iconUrl = jstr(jt, "icon");
                out.push_back(std::move(t));
            }
        }
        catch (...) {}
    }
    return out;
}
