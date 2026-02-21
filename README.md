> **Model Used: Claude Sonnet 4.6**

# Armoury — GW2 Nexus Addon

A [Nexus](https://raidcore.gg/Nexus) addon for Guild Wars 2 that lets you save, load, and manage equipment and traitline build templates — like the BlishHUD Template Manager, but for Nexus.

---

## Features

- Save and name any number of build templates (traits, skills, equipment)
- Load a saved template with a single click — generates and copies the chat code
- Supports all professions
- Validates your GW2 API key and checks for required `characters` + `builds` permissions
- Build chat code encode/decode (`[&...]` format)
- Persistent templates stored to disk (JSON)
- Nexus quick-access bar icon and keybind support

---

## Installation

1. Download `Armoury.dll` from the [latest release](../../releases/latest).
2. Place it in your Nexus addons folder (typically `Guild Wars 2/addons/`).
3. Launch the game.
4. Load the addon from your Nexus library.

---

## Usage

1. Open the Armoury window via the quick-access bar icon or your assigned keybind.
2. Enter your GW2 API key in the Options panel (needs `characters` and `builds` permissions).
3. Select a character from the dropdown to load their current build.
4. Click **Save Template** to store it under a name of your choice.
5. Select any saved template and click **Load** to copy the chat code to your clipboard.

---

## Building from Source

### Prerequisites

- Visual Studio 2022 (MSVC) or Build Tools with the MSVC compiler
- CMake 3.20+
- Ninja (included with VS installer)
- Git

### Steps

```powershell
git clone https://github.com/YoruDev-Ryland/GW2-Nexus---Armoury.git
cd GW2-Nexus---Armoury

# Configure
cmake -B build -G "Ninja" `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_CXX_COMPILER=cl `
  -DCMAKE_C_COMPILER=cl

# Build
cmake --build build --parallel

# Output: build/Armoury.dll
```

CMake automatically fetches all dependencies (Nexus API header, ImGui v1.80, nlohmann/json) on first configure.

---

## Architecture

```
entry.cpp           DllMain + GetAddonDef + AddonLoad/Unload
Shared.h/.cpp       Global pointers: APIDefs, Self, MumbleLink, MumbleIdent
Settings.h/.cpp     Persistent settings (JSON)
GW2Api.h/.cpp       GW2 REST API calls (tokeninfo, characters, builds)
ChatCode.h/.cpp     Build chat code encode/decode ([&...] format)
TemplateStore.h/.cpp  In-memory + on-disk template management
UI.h/.cpp           All ImGui rendering callbacks
```

---

## Customising the Quick-Access Icon

Replace `src/icon.png` with your own 64×64 or 128×128 RGBA PNG, then rebuild.

---

## Contributing

Pull requests welcome.

---

## License

MIT