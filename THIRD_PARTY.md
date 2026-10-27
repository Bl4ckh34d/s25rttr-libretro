# Third-party source and notices

This repository distributes integration source and patches, rather than vendored dependency trees or game assets. Existing copyright notices in the patched files remain intact when applying patches. Dependencies retain their own licenses; consult the pinned sources before redistributing binaries or resources.

| Component | Source / notice location |
| --- | --- |
| Android integration baseline | `upstream.json`, upstream root `LICENSE` |
| Return to the Roots | `app/jni/s25client/LICENSE` and `LICENSES/` in the pinned tree; GPL-2.0-or-later and file-specific notices |
| SDL2 | `external/SDL/LICENSE.txt`; zlib license |
| SDL_mixer | `external/SDL_mixer/LICENSE.txt` and bundled codec notices; zlib license for the mixer |
| gl4es | `external/gl4es/LICENSE` in the pinned tree |
| Boost, Lua, bzip2, miniupnpc, libsamplerate and remaining dependencies | Their pinned source trees and individual license files |
| libretro API header | Copyright and MIT license retained in `libretro/include/libretro.h` |

The root `LICENSE` contains the GPL version 2 text. Newly authored integration source is GPL-2.0-or-later. Source patches to SDL and SDL_mixer are provided under their respective upstream zlib licenses. No rights to the original game's data, music or artwork are granted by this repository.
