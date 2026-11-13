# Attribution and licenses

This is a modified native macOS port, not an official game release.

- Storm Engine: https://github.com/storm-devs/storm-engine, source snapshot a68f43d with subsequent ARM64/CoAS port changes. GNU GPL version 3; see `LICENSE` and `engine/LICENSE.txt`. Existing author notices in source files are retained.
- DXVK Native: https://github.com/Joshua-Ashton/dxvk-native, source snapshot a2dc99c with Darwin, shader specialization and projection-invariant fog changes. zlib/libpng license; see `dxvk/LICENSE`.
- Wine-derived DirectX compatibility headers in `dxvk/include/native/directx` retain their original notices and LGPL-2.1-or-later terms. See `LICENSES/LGPL-2.1.txt`.
- stb_vorbis by Sean Barrett: the original dual MIT/public-domain license is preserved in `engine/audio/stb_vorbis.c`.
- Other embedded third-party source files retain their individual copyright and license notices.

The build downloads pinned upstream SDL2 (zlib), mimalloc (MIT), spdlog (MIT), fmt (MIT) and fast_float (MIT/Apache-2.0) sources. Their notices remain in the downloaded source trees under the ignored build directory. Vulkan/MoltenVK, SPIRV-Tools and build utilities are installed separately through Homebrew.

The SDL audio backend is a port implementation with its own small internal interface. No FMOD SDK headers, FMOD libraries or FMOD runtime are included or required. Remote crash reporting and telemetry are disabled and their former SDK dependency is removed.

Commercial game scripts, textures, models, audio, videos and saves are not distributed. You must supply your own compatible game installation. The engine techniques under `engine/src/techniques` are from the upstream open-source engine, not a copy of a user's game RESOURCE directory.
