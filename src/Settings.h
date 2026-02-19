#pragma once
#include <string>

// ── Settings persisted to <addondir>/settings.json ────────────────────────────
struct Settings
{
    std::string ApiKey;            // GW2 API key (requires "characters" + "builds")
    bool        ShowWindow  = true;
    bool        ShowIcons   = true;  // Fetch and show skill/item icons
    int         WindowWidth  = 680;
    int         WindowHeight = 520;

    // Load from / save to disk. Path resolved via APIDefs->Paths_GetAddonDirectory.
    void Load();
    void Save() const;
};

extern Settings g_Settings;
