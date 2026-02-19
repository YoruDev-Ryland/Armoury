#include "Settings.h"
#include "Shared.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <string>

using json = nlohmann::json;

Settings g_Settings;

// Returns the full path to settings.json, e.g.:
//   C:\...\Guild Wars 2\addons\Armoury\settings.json
static std::string SettingsPath()
{
    if (!APIDefs || !APIDefs->Paths_GetAddonDirectory)
        return "";

    std::string dir = APIDefs->Paths_GetAddonDirectory("Armoury");
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\settings.json";
}

void Settings::Load()
{
    std::string path = SettingsPath();
    if (path.empty()) return;

    std::ifstream f(path);
    if (!f.is_open()) return; // first run — use defaults

    try
    {
        json j    = json::parse(f);
        ApiKey       = j.value("ApiKey",       "");
        ShowWindow   = j.value("ShowWindow",   true);
        ShowIcons    = j.value("ShowIcons",    true);
        WindowWidth  = j.value("WindowWidth",  680);
        WindowHeight = j.value("WindowHeight", 520);
    }
    catch (...) { /* malformed json — use defaults */ }
}

void Settings::Save() const
{
    std::string path = SettingsPath();
    if (path.empty()) return;

    json j;
    j["ApiKey"]       = ApiKey;
    j["ShowWindow"]   = ShowWindow;
    j["ShowIcons"]    = ShowIcons;
    j["WindowWidth"]  = WindowWidth;
    j["WindowHeight"] = WindowHeight;

    std::ofstream f(path);
    if (f.is_open())
        f << j.dump(4);
}
