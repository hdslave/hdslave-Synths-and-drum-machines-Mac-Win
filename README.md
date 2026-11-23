# hdslave Synths & Drum Machines (macOS & Windows CLAP)

Desktop CLAP port of all 11 **hdslave** synthesizers, drum machines, and effects for **FL Studio** (2024+) and any other CLAP host on **macOS** (Apple Silicon / Intel) and **Windows** (x64).

## Included Plug-ins (11)

| Plug-in | Parameters | Modules | Factory Presets |
| --- | ---: | ---: | ---: |
| **Anvil** | 54 | 8 | 10 |
| **Cortex Filter** | 46 | 6 | 15 |
| **Foundry** | 703 | 18 | 9 |
| **Machine** | 139 | 13 | 36 |
| **MoonBass** | 42 | 9 | 10 |
| **Mystik** | 116 | 9 | 15 |
| **Necronomicon** | 58 | 14 | 6 |
| **Nexus** | 134 | 11 | 25 |
| **Psychic** | 71 | 13 | 7 |
| **Reverie** | 80 | 13 | 20 |
| **TB-MPC-3-P-0** | 30 | 3 | 6 |

## Desktop Features

- **Expanded High-Resolution Control Panels (`1360×820`, resizable up to `3200×2000`, Retina / HiDPI)**:
  - Styled to match each instrument's live screen palette using the **Departure Mono** pixel font (OFL 1.1), chamfered hardware cards, neon accent tags, 25-segment LED-ring rotary knobs (with center-lit bipolar arcs), illuminated toggles, momentary press buttons, choice dropdowns with LED position bars, and interactive step-fader banks (`Pitch 1..8`, `Velocity 1..8`).
  - Dirty-rect incremental rendering at 60 fps (`< 1 ms` full frame) with immediate UI value hold for smooth, non-jumpy knob dragging.
- **Interactive Live Display (`PAGE 2X` / `PAGE 1X`)**:
  - Mounts each plug-in's real-time `320×218` visual display alongside dedicated **ENCODER**, **`-` / `PUSH` / `+` / `SHIFT`**, and live-bound **`K1`–`K3`** knobs.
  - Drag or click directly on the bottom **K1–K3 strip** inside the display to edit K1–K3; drag the **Cortex Filter** 3D cube in 2D to control Morph (X) & Freq (Y); drag/scroll/click anywhere else on the display to turn/push the encoder; right-click left/right halves to nudge.
  - Supports mouse drag, `Shift`+drag fine control, double-click / `Alt`-click reset, mouse wheel, two-finger trackpad scroll & rotation, arrow/Enter/Escape keys, and Windows touch/pen drag & long-press menus.
- **Built-in MIDI CC Mapping & MIDI Learn**:
  - Click **MIDI LEARN** or right-click any control and turn a hardware MIDI CC knob to bind it immediately (saved in the DAW project state, with backward-compatible raw state loading).
- **Native Zero-Bloat GUI Backends**:
  - **macOS**: Cocoa (`NSView` + CoreGraphics, `CLAP_WINDOW_API_COCOA`).
  - **Windows**: Win32 (`HWND` + `StretchDIBits`, `CLAP_WINDOW_API_WIN32`).

## Build & Install

### macOS (Apple Silicon / Intel)

Requires Xcode Command Line Tools, CMake 3.20+, and Ninja:

```bash
./scripts/build-macos.sh
./scripts/install-macos.sh
```

This builds, ad-hoc signs, and runs the strict host/preset/audio test suite across all 11 `.clap` bundles, then copies them to `/Library/Audio/Plug-Ins/CLAP/` (pass `--user` for `~/Library/Audio/Plug-Ins/CLAP/`).

In **FL Studio**: open **Options → Manage Plugins → Find installed plugins**.

### Windows (x64)

Requires Visual Studio 2022 (C++ desktop workload) and CMake 3.20+:

```powershell
.\scripts\build-windows.ps1
```

Copy the built `.clap` files from `build\windows\clap\Release\` to `C:\Program Files\Common Files\CLAP\`.

## Licences & Third-Party Code

- **CLAP SDK** (`third_party/clap`): MIT Licence.
- **Departure Mono** (`licenses/OFL-departure-mono.txt`): SIL Open Font Licence 1.1.
- **Nexus DSP** (`plugins/nexus/third_party/eurorack`): MIT Licence (Émilie Gillet / Mutable Instruments Plaits code).
