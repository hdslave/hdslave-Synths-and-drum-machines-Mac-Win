# Build the desktop CLAP suite

```sh
git clone --recurse-submodules https://github.com/hdslave/hdslave-Synths-and-drum-machines-Mac-Win.git
```

Run the commands below from the cloned repository. Prebuilt Apple Silicon, Intel Mac and Windows x64 editions of [Slink are available here](https://github.com/hdslave/hdslave-Slink-Synth/releases/tag/v0.6.0).

## macOS (Apple Silicon / Intel)

Requires Xcode Command Line Tools and CMake 3.20+ (Ninja optional):

```bash
./scripts/build-all-macos.sh
./scripts/install-all-macos.sh
```

`install-all-macos.sh` builds, ad-hoc signs, tests, and copies all 11 `.clap` bundles into `/Library/Audio/Plug-Ins/CLAP/` (pass `--user` for `~/Library/Audio/Plug-Ins/CLAP/`). Quit (`Cmd+Q`) and reopen **FL Studio**, then open **Options → Manage Plugins → Find installed plugins**.

To build or install a single plug-in directly from its submodule:

```bash
cd Reverie/mac-clap
./install-macos.sh
```

## Windows (x64)

Requires Visual Studio 2022 (C++ desktop workload) and CMake 3.20+:

```powershell
.\scripts\build-all-windows.ps1
```

Copy the built `.clap` files from each `<Plugin>\mac-clap\build\windows\clap\Release\` folder to `C:\Program Files\Common Files\CLAP\`.

## Keeping It Current

Submodule folders stay at their pinned commits until updated. Run `tools/update.sh` to advance every plug-in folder to the latest commit on `main` and commit the updated pins; then `git push` to publish.

## Trademarks &amp; Licences

- **CLAP SDK**: MIT Licence.
- **Departure Mono** (`mac-clap/licenses/OFL-departure-mono.txt`): SIL Open Font Licence 1.1.
- **Nexus DSP** (`third_party/eurorack`): MIT Licence (Émilie Gillet / Mutable Instruments Plaits code).
- Product names mentioned here are trademarks of their respective owners (Roland, Moog Music, Behringer, Jomox, Elektron, Korg, Pioneer DJ / AlphaTheta, E-mu, Rossum Electro-Music, Mutable Instruments, Image-Line, inMusic Brands). These projects are not affiliated with or endorsed by any of them.
