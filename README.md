# Corsairs Native — City of Abandoned Ships on Apple Silicon

An experimental native ARM64 port of **Sea Dogs / Age of Pirates 2: City of Abandoned Ships** for macOS. No Wine, Windows VM or Rosetta is used.

```text
Your game PROGRAM + RESOURCE
           ↓
Storm Engine C++ / ARM64
           ↓
D3D9 → DXVK Native → Vulkan → MoltenVK → Metal

SDL2: window, keyboard, relative mouse and WAV/OGG audio
```

This repository contains the modified engine, modified DXVK, an SDL audio backend and installation tools. **It does not contain the commercial game or saves.** Supply your own legally obtained compatible game installation. This is not an official game release.

## Requirements

- Apple Silicon Mac, macOS 15 or newer; run Terminal natively, not through Rosetta.
- Xcode Command Line Tools (`xcode-select --install`).
- [Homebrew](https://brew.sh/) and [uv](https://docs.astral.sh/uv/).
- Internet access for the first dependency installation/build.
- A compatible CoAS game directory containing `RESOURCE` and either `PROGRAM/seadogs.c` or a supported packed `stormex2.dll` script archive.

The validated data variant is Russian CoAS rev.3. Other editions are not certified. GOG/Steam ownership alone does not guarantee that a different script/data revision is compatible. This is not a port of later Sea Dogs games.

## Install

```sh
git clone https://github.com/abrekhov/corsairs-native.git
cd corsairs-native
brew bundle --no-upgrade
uv run corsairs.py build
uv run corsairs.py prepare --game "/path/to/City of Abandoned Ships"
```

`brew bundle --no-upgrade` installs missing uv, CMake, Ninja, Meson, pkgconf, glslang, Vulkan headers/loader, MoltenVK and SPIRV-Tools without requesting upgrades of already installed formulae. CMake downloads checksum-pinned SDL **2.30.12**, mimalloc **2.1.2**, fmt **10.2.1**, spdlog **1.14.1** and fast_float **3.4.0** into the ignored `build/` directory. No Conan installation or pre-existing dependency cache is required. The build uses two workers by default; `uv run corsairs.py build --jobs 4` changes this.

Preparation creates a separate runtime, by default:

```text
~/Library/Application Support/CorsairsNative/
```

To choose another location:

```sh
uv run corsairs.py prepare --game "/path/to/game" --runtime "/path/to/separate-runtime"
```

Do not point the runtime at the original game folder or an unrelated nonempty directory. Configuration containing your local paths is stored in the ignored `.local/settings.json`; do not upload this file or the runtime.

### What preparation changes

- Original game files are not overwritten. Models, textures and audio are referenced from the selected game directory; do not move it afterwards without preparing again.
- PROGRAM and RESOURCE/INI text is copied into the runtime and converted from CP1251/UTF-16 to UTF-8 as necessary.
- For the supported `120DT` packed-script format, PROGRAM is extracted locally. Stray unmatched closing parentheses are removed from code in the prepared copy, with strings/comments preserved. The number of repairs is reported. This is syntax compatibility preparation, not a byte-identical copy or a redesign of quests/dialogues.
- The engine's own upstream techniques/shared headers replace the runtime technique/shared view. Commercial game data is not bundled in this repository.
- Saves are separate. Existing saves from your game installation are not automatically imported, replaced or removed.

## Run

First try a **bounded windowed** run:

```sh
uv run corsairs.py test --timeout 30
```

The watchdog intentionally stops this smoke test at its deadline; exit 124 means the deadline was reached, not that gameplay correctness was verified.

For regular play:

```sh
uv run corsairs.py play
uv run corsairs.py play --window fit
```

The default is a normal macOS fullscreen Space without a display-mode switch. `fit` is a display-sized desktop window. Exclusive fullscreen/display-mode changes are not used. Relative mouse capture is enabled: **Esc** releases it, a click recaptures it; **F11** selects fit and **F12** toggles Space. Cmd-Tab releases capture while the game is inactive.

An independent watchdog is armed before the engine. Development tests have a fixed deadline. Regular play has no fixed playtime limit but stops an unresponsive engine after a 90-second missing-heartbeat interval. Test cleanup targets the test process group, not unrelated applications. Do not run GUI tests while another game instance is active.

Logs are under `<runtime>/runs/` and `<runtime>/user/Logs/`. Saves are under `<runtime>/save/`; back them up separately. Never copy E2E fixture saves over your own saves.

## Script cache

`config/engine.ini` enables `cache_mode = 2`: source contents/names and debug-bytecode settings are checked when each engine process starts. Restart the game after changing PROGRAM. Set `cache_mode = 0` in your runtime's `engine.ini` to disable it. Corrupt, truncated or incompatible cache files are rejected and rebuilt. The cache is not a portable save format.

## Verification

After building and preparing data, with no other game instance running:

```sh
uv run tests/safe-run-e2e.py
uv run tests/script-cache-e2e.py --mode 2
uv run tests/save-state-e2e.py --cache-mode 2
```

These exercise independent watchdog cleanup, the real engine/script VM, cache invalidation, save/load of live/expired attribute references and preservation of the previous save when its writer is interrupted. GPU readback sources for sea shaders and fog are in `tests/`.

See [VERIFICATION.md](VERIFICATION.md) for actual release checks and limitations. A successful build or a watchdog deadline is not proof of complete game compatibility. This source build has not been certified on every clean Mac or game edition.

## Scope and known limitations

- Native menu, new game, gameplay scenes, real audio, loading/saving, sea rendering and bounded windowed focus/input scenarios have been exercised during development.
- Fixes include retired sound handles, stale attribute-reference serialization, atomic replacement of saves, Apple PS1 sampler specialization, projection-invariant island fog and script-cache correctness.
- Long macOS Space sessions, every location/quest and the complete overnight → chest → door route are not comprehensively verified. Remaining particle/worldmap technique warnings and incomplete legacy network-script support are not claimed fixed.
- This is a source build requiring uv/Homebrew, not a self-contained signed/notarized drag-and-drop application.
- No FMOD SDK/runtime is distributed. The internal SDL backend implements the audio subset used by this port; it is not a full replacement for every FMOD feature.
- Direct Metal experiments, Wine, old development logs/binaries and user data are intentionally excluded.

## License

The engine and port code are distributed under GPLv3. DXVK and embedded third-party files retain their respective licenses. See [NOTICE.md](NOTICE.md), [LICENSE](LICENSE) and `LICENSES/`. Game content remains subject to its original owners' terms and is not included.
