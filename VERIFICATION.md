# Verification

Release preparation checks were performed on an existing Apple Silicon macOS development machine, using a new source-export directory and a new CMake build. This is not certification on a factory-clean Mac or every macOS/game edition.

## Observed results

| Check | Result |
|---|---|
| `brew bundle --no-upgrade` and dependency check | All 10 Brewfile dependencies satisfied |
| `uv run corsairs.py build` | Native ARM64 engine and DXVK build succeeds without Conan or FMOD SDK |
| Packed-script preparation | 934 scripts extracted locally; 1044 prepared text files including INI; 52 unmatched closing parentheses repaired |
| Watchdog E2E | 6/6 scenarios pass, including SIGSTOP, killed launcher, unrelated-process isolation and heartbeat expiry |
| Script cache mode 2 E2E | 9 scenarios pass, including exact constants, unchanged source, same-mtime/size edits, corruption, version and debug-setting invalidation |
| Save-state E2E with cache mode 2 | Round-trip references and interrupted-writer preservation pass |
| Sea shader GPU readback | 4/4 pass: Sea2/Sea2_SunRoad with 2D and volume textures |
| Fog GPU readback | 6/6 pass, including invariant perspective depth across near/far slices |
| Real game smoke test | A copied sea save loads with the exported engine and freshly extracted/prepared scripts; water, UI and distant-island haze are visible |
| Audio backend | SDL audio device initializes; active mixer channels produce nonzero sample peaks |
| Original user saves | Before/after hashes unchanged during package verification; no saves are included in the repository |
| Public source audit | detect-secrets scan with remote verification disabled: zero findings; local user-path/infrastructure filters found only the intentional public GitHub clone URL |

The real-game smoke run was windowed and ended at its independent watchdog deadline. That exit is not a normal game exit or proof that every quest/transition works. Screenshots and development logs are intentionally not included in the public repository.

## Reproduce

Build and prepare your own supported game data as described in README, then:

```sh
uv run tests/safe-run-e2e.py
uv run tests/script-cache-e2e.py --mode 2
uv run tests/save-state-e2e.py --cache-mode 2
```

GPU checks use the engine's own open-source shader resources, an actual D3D9 → Vulkan → Metal device and pixel readback:

```sh
for name in sea fog; do
  clang++ -std=c++20 tests/$name-render-e2e.cpp \
    -Ibuild/_deps/sdl2-src/include \
    -Idxvk/include/native/directx -Idxvk/include/native/windows \
    -L"$PWD/build/bin" -Wl,-rpath,"$PWD/build/bin" \
    -lSDL2-2.0.0 -ldxvk_d3d9 -o build/$name-render-e2e
  uv run run-native.py --timeout 25 -- "$PWD/build/$name-render-e2e"
done
```

Do not run GUI checks while another game instance is active. All engine/probe launches must use the independent watchdog; never use exclusive fullscreen/display-mode switching for development.

## Not claimed

- No exhaustive compatibility with all game editions, locations, quests, particles, networking or long fullscreen Space sessions.
- No factory-clean Mac validation, signed/notarized application or dependency-free binary distribution.
- No complete FMOD feature parity: this is the SDL audio subset used by this port.
- No measured FPS guarantee, fully cold OS startup time or guarantee of crash-free gameplay.
