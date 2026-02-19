#include "Shared.h"
#include "Settings.h"
#include "TemplateStore.h"
#include "GW2Api.h"
#include "UI.h"

#include <imgui.h>
#include <windows.h>
#include <cstring>

// ── DllMain ───────────────────────────────────────────────────────────────────

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason, LPVOID /*lpReserved*/)
{
    switch (ul_reason)
    {
        case DLL_PROCESS_ATTACH:
            Self = hModule;
            DisableThreadLibraryCalls(hModule);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

// ── Keybind handler (plain C function pointer — no lambdas with captures) ──────

static void ProcessKeybind(const char* aIdentifier, bool aIsRelease)
{
    if (aIsRelease) return;

    if (strcmp(aIdentifier, "KB_ARMOURY_TOGGLEVIS") == 0)
    {
        g_Settings.ShowWindow = !g_Settings.ShowWindow;
        g_Settings.Save();
    }
}

// ── Addon lifecycle ───────────────────────────────────────────────────────────

static void AddonLoad(AddonAPI_t* aApi)
{
    APIDefs = aApi;

    // ── Set up ImGui to share the context Nexus is already running ────────────
    // These two calls MUST come before any ImGui API usage.
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(aApi->ImguiContext));
    ImGui::SetAllocatorFunctions(
        reinterpret_cast<void*(*)(size_t, void*)>(aApi->ImguiMalloc),
        reinterpret_cast<void(*)(void*, void*)>(aApi->ImguiFree));

    // ── Grab shared Nexus data pointers ───────────────────────────────────────
    MumbleLink  = static_cast<Mumble::LinkedMem*>(
                      aApi->DataLink_Get(DL_MUMBLE_LINK));
    MumbleIdent = static_cast<Mumble::Identity*>(
                      aApi->DataLink_Get(DL_MUMBLE_LINK_IDENTITY));

    // ── Load persisted settings ───────────────────────────────────────────────
    g_Settings.Load();

    // ── Initialise template store (loads armoury.json from disk) ─────────────
    TemplateStore::Init();

    // ── Register render callbacks ─────────────────────────────────────────────
    aApi->GUI_Register(RT_Render,        UI::Render);
    aApi->GUI_Register(RT_OptionsRender, UI::RenderOptions);

    // ── Register toggle keybind ───────────────────────────────────────────────
    aApi->InputBinds_RegisterWithString(
        "KB_ARMOURY_TOGGLEVIS",
        ProcessKeybind,
        "(null)"); // user assigns binding in Nexus keybind settings

    // ── Load quick-access icon (resource ID 101 embedded in the DLL) ─────────
    aApi->Textures_GetOrCreateFromResource("ICON_ARMOURY",       101, Self);
    aApi->Textures_GetOrCreateFromResource("ICON_ARMOURY_HOVER", 101, Self);

    // ── Add quick-access shortcut ─────────────────────────────────────────────
    aApi->QuickAccess_Add(
        "QA_ARMOURY",
        "ICON_ARMOURY",
        "ICON_ARMOURY_HOVER",
        "KB_ARMOURY_TOGGLEVIS",
        "Armoury");

    // ── Kick off background resolution if we already have templates ───────────
    if (!g_Settings.ApiKey.empty())
        TemplateStore::RequestResolve(g_Settings.ApiKey);

    aApi->Log(LOGL_INFO, "Armoury", "Armoury loaded.");
}

static void AddonUnload()
{
    if (!APIDefs) return;

    // ── Flush background resolve thread and persist data ─────────────────────
    TemplateStore::Shutdown();

    // ── Deregister everything registered in AddonLoad ─────────────────────────
    APIDefs->GUI_Deregister(UI::Render);
    APIDefs->GUI_Deregister(UI::RenderOptions);
    APIDefs->InputBinds_Deregister("KB_ARMOURY_TOGGLEVIS");
    APIDefs->QuickAccess_Remove("QA_ARMOURY");

    // ── Persist final settings ────────────────────────────────────────────────
    g_Settings.Save();

    APIDefs->Log(LOGL_INFO, "Armoury", "Armoury unloaded.");

    APIDefs    = nullptr;
    MumbleLink = nullptr;
    MumbleIdent= nullptr;
}

// ── Addon definition — the only exported symbol Nexus needs ──────────────────

static AddonDefinition_t s_AddonDef{};

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    s_AddonDef.Signature   = 0xAA4D0001; // Unique ID — change before publishing if needed
    s_AddonDef.APIVersion  = NEXUS_API_VERSION;
    s_AddonDef.Name        = "Armoury";
    s_AddonDef.Version     = { 1, 0, 0, 0 };
    s_AddonDef.Author      = "YoruDev-Ryland";
    s_AddonDef.Description = "Save and manage equipment and traitline templates, "
                             "like Blish HUD Template Manager — for Nexus.";
    s_AddonDef.Load        = AddonLoad;
    s_AddonDef.Unload      = AddonUnload;
    s_AddonDef.Flags       = AF_None;
    s_AddonDef.Provider    = UP_GitHub;
    s_AddonDef.UpdateLink  = "https://github.com/YoruDev-Ryland/GW2-Nexus---Armoury";

    return &s_AddonDef;
}
