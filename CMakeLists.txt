cmake_minimum_required(VERSION 3.20)

project(Armoury VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies via FetchContent ─────────────────────────────────────────────
include(FetchContent)

# Nexus API header (provides AddonAPI_t, AddonDefinition_t, etc.)
FetchContent_Declare(
    nexus_api
    GIT_REPOSITORY https://github.com/RaidcoreGG/RCGG-lib-nexus-api.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nexus_api)

# Dear ImGui — MUST be pinned to v1.80 (same version Nexus ships).
# Using a different version causes crashes in SetCurrentContext.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.80
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

# nlohmann/json — header-only JSON for settings and API responses
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# ── Embed icon.png as a Win32 resource ───────────────────────────────────────
# configure_file expands @CMAKE_CURRENT_SOURCE_DIR@ so rc.exe can find the
# PNG regardless of which directory it is invoked from.
configure_file(
    src/resources.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/resources.rc
    @ONLY)

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/entry.cpp
    src/Shared.cpp
    src/Settings.cpp
    src/GW2Api.cpp
    src/ChatCode.cpp
    src/TemplateStore.cpp
    src/UI.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/resources.rc

    # ImGui core — compiled inside our DLL, context shared with Nexus at runtime
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

add_library(Armoury SHARED ${SOURCES})

# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(Armoury PRIVATE
    src
    ${nexus_api_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(Armoury PRIVATE
    nlohmann_json::nlohmann_json
    winhttp      # WinHTTP for GW2 REST API calls (Windows built-in)
)

# ── Compiler / linker flags ───────────────────────────────────────────────────
if(MSVC)
    # Remove default /W3 so we can set our own warning level without duplicates
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

    target_compile_options(Armoury PRIVATE
        /W3         # Reasonable warnings
        /WX-        # Don't treat warnings as errors (imgui has some)
        /MP         # Parallel compilation
        /EHsc       # C++ exceptions
        /permissive-
        # Suppress noisy warnings from third-party headers
        /wd4100     # unreferenced formal parameter
        /wd4189     # local variable initialized but not referenced
    )

    # NOMINMAX prevents windows.h min/max macros from clashing with std::min/max
    target_compile_definitions(Armoury PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    target_link_options(Armoury PRIVATE
        /SUBSYSTEM:WINDOWS
    )
endif()

# ── Output ────────────────────────────────────────────────────────────────────
set_target_properties(Armoury PROPERTIES
    OUTPUT_NAME   "Armoury"
    PREFIX        ""            # Don't prepend "lib" on MinGW
    SUFFIX        ".dll"
)
