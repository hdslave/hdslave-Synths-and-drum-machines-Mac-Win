# hdslave Desktop CLAP (macOS & Windows)

Desktop port of all 11 **hdslave** CLAP synthesizers, drum machines, and effects for **FL Studio** (2024+) and any other CLAP host on **macOS** (Apple Silicon / Intel) and **Windows** (x64).

## Included Plug-ins (11)

| Plug-in | Parameters | Modules |
| --- | ---: | ---: |
| **Anvil** | 54 | 8 |
| **Cortex Filter** | 46 | 6 |
| **Foundry** | 703 | 18 |
| **Machine** | 139 | 13 |
| **MoonBass** | 42 | 9 |
| **Mystik** | 116 | 9 |
| **Necronomicon** | 58 | 14 |
| **Nexus** | 134 | 11 |
| **Psychic** | 71 | 13 |
| **Reverie** | 80 | 13 |
| **TB-MPC-3-P-0** | 30 | 3 |

## Desktop Features

- **Expanded High-Resolution Control Panels (`1360×820`, resizable up to `2560×1600`)**: Replaces paged hardware lists with massive multi-module hardware control panels, grouping parameters by module with rotary knobs, illuminated toggle switches, and stepped choice selectors.
- **Embedded Live Visual Monitor**: Hosts each plug-in's live `320×218` display directly inside the control panel (clickable and scrollable).
- **Direct Mouse & Drag Control**:
  - **Click & vertical drag**: adjust any rotary knob or stepped selector.
  - **Hold `Shift` + drag**: fine-resolution control.
  - **Double-click**: reset parameter to default.
  - **Mouse wheel**: nudge hovered control, or scroll the control wall.
  - Emits full CLAP parameter gestures so FL Studio's **Last Tweaked**, **Automation Clips**, and **Link to Controller** work natively.
- **Built-in MIDI CC Mapping & MIDI Learn**:
  - Click **MIDI LEARN** (or **right-click** any control) and move a hardware MIDI CC knob to bind it immediately.
  - Mapped controls display a `C<num>` badge.
  - MIDI CC maps are saved inside the DAW project state (while remaining backward-compatible with raw MPC states).
- **Factory Preset Browser**: Header `<` / `>` buttons and multi-column preset picker overlay using CLAP preset discovery.
- **Native Zero-Bloat GUI Backends**:
  - **macOS**: Native Cocoa (`NSView` + CoreGraphics, `CLAP_WINDOW_API_COCOA`).
  - **Windows**: Native Win32 (`HWND` + `StretchDIBits`, `CLAP_WINDOW_API_WIN32`).

## Build & Install

### macOS (Apple Silicon / Intel)

Requires Xcode Command Line Tools, CMake 3.20+, and Ninja.

```bash
./scripts/build-macos.sh
./scripts/install-macos.sh
```

This builds, ad-hoc signs, and tests all 11 `.clap` bundles, then copies them to `~/Library/Audio/Plug-Ins/CLAP/`.

In **FL Studio**: open **Options → Manage Plugins → Find installed plugins** and load any `hdslave` CLAP plug-in.

### Windows (x64)

Requires Visual Studio 2022 (C++ desktop workload) and CMake 3.20+.

```powershell
.\scripts\build-windows.ps1
```

Copy the built `.clap` files from `build\windows\clap\Release\` to `C:\Program Files\Common Files\CLAP\`.

## Syncing Engine Updates from MPC Repos

To pull updated DSP/screen sources from sibling MPC plug-in repositories:

```bash
./scripts/sync-from-mpc.sh
```
