# s25rttr-libretro

An Android ARMv7 libretro core for **Return to the Roots**, the open-source engine for **The Settlers II / Die Siedler II**. Play inside RetroArch with a gamepad-controlled mouse, native game saves and RetroArch save states.

This repository contains integration source, tests and patches. **No original game files, music, artwork or save games are included.** You must supply your own compatible Settlers II data. Tested with German game data on a 32-bit Android 11 TV and an Xbox One Bluetooth controller. Other architectures and frontends have not been validated.

## Features

- Left stick mouse movement, with RT/R2 for precision.
- A/B mouse clicks, D-pad/right-stick map scrolling and building display shortcuts.
- Music scheduling independent of rendering frame rate.
- Fast-forward using RetroArch's hotkeys and configured rate, including unlimited mode.
- RetroArch save/load slots, including loading after a core restart.
- In-memory game recovery after graphics-context loss when switching away from the app.

Save states restore the world, local player, camera, zoom, building overlays and pause state. Music playback position and temporary open windows are not restored. Saving requires an active single-player game; loading also works from the game menu. Rewind, run-ahead and netplay are **not supported**. RetroArch Reset stops the current game and restarts the engine at its main menu. Disk save states remain available. An Android process kill loses the in-memory recovery snapshot; save to disk before leaving a session.

## Build

Requirements: Git, Python 3, CMake 3.24+, Ninja and Android NDK r27d. The Windows build helper uses PowerShell. The CMake project can also be invoked directly with an Android toolchain; only the Windows-to-ARMv7 workflow has been tested.

```powershell
python bootstrap.py
$env:ANDROID_NDK_HOME = 'C:/path/to/android-ndk-r27d'
./libretro/build.ps1
```

`bootstrap.py` fetches the upstream Android port at the revision in `upstream.json`, initializes its pinned submodules and applies the three patches. Sources remain under `deps/`; nothing is installed on a device. CMake fetches the pinned libsamplerate dependency when necessary. The build helper accepts `-CMake`, `-Ninja`, `-Upstream`, `-SampleRateSource` and `-Jobs` overrides. Output: `release/rttr_libretro_android.so`.

To reuse an existing checkout, run `python bootstrap.py --source PATH`. It checks revisions and patch applicability rather than resetting existing work. `--check` verifies an already patched checkout without changing it. Review any unrelated modifications before building from a reused checkout.

## Game data and resources

Keep the original `S2.EXE`, `DATA/` and `GFX/` together. Load `S2.EXE` as the content entry; the executable is only a content marker and is not executed. The current core still requires that filename/format; a separate `.rttr` marker is not implemented.

The core also needs the pinned RTTR resources under:

```text
<RetroArch system directory>/s25rttr/RTTR/
  assets/base/splash.bmp
  ...remaining RTTR resources...
```

Use the resource tree from `deps/s25rttr-android/app/jni/s25client/data/RTTR`, together with the corresponding [Return to the Roots](https://www.siedler25.org/) runtime resources. Compile translations from its `external/languages/rttr-*.po` files with gettext `msgfmt` into `RTTR/languages/<language>/LC_MESSAGES/rttr.mo`. Music is supplied separately by the user in the RTTR resource layout; no music download is performed by this repository.

Native settings and saves go to `<RetroArch save directory>/s25rttr/`. RetroArch manages save-state files separately. Keep these directories when updating the core.

## RetroArch installation

Install the `.so` through RetroArch's **Install or Restore a Core** flow where available. Android builds that export `CoreSideloadActivity` can also install a readable core file with its `LIBRETRO` intent extra.

Copy `libretro/retroarch/rttr_libretro.info` to the core-info directory. The example core override in `libretro/retroarch/Return to the Roots.cfg` selects GL, shared context, unthreaded rendering and disables analog-to-D-pad conversion for gameplay. **Adjust its system/save directory paths to your device.** It disables rewind and run-ahead. `core_info_savestate_bypass` is included for Android installations whose private info directory cannot be updated; it is scoped to this core.

Use RetroArch's normal **Save State / Load State** commands or bind your preferred hotkeys. Stick-click save/load bindings used during testing were frontend settings, not hardcoded core controls.

Use RetroArch's normal fast-forward hold/toggle hotkeys and **Settings > Frame Throttle > Fast-Forward Rate**. The core reads `GET_THROTTLE_STATE` and advances the local simulation at the frontend's requested rate. A rate of zero means unlimited, subject to hardware performance. Input and rendering still run once per frontend frame; additional simulation steps have an 8 ms work budget so unlimited fast-forward remains responsive. Releasing fast-forward returns immediately to the game's selected speed. Music keeps its normal real-time clock; frontend audio settings remain under RetroArch's control. Older frontends that expose only `GET_FASTFORWARDING` fall back to unlimited acceleration because they do not provide a numerical rate.

## Xbox controls

| Control | Action |
| --- | --- |
| Left stick | Mouse |
| A / B | Left / right mouse button |
| Right stick / D-pad | Scroll map |
| RT / R2 | Hold for precise mouse |
| X | Construction sites |
| Y | Building names |
| RB | Productivity percentages |
| LB | Minimap |
| Menu | Escape / close window |

LT/L2, View and stick clicks remain available for frontend hotkeys. Disable RetroArch analog-to-D-pad conversion for this core so the left stick remains a mouse.

## Validation and limits

The original integration was built and tested on the TV: smooth gameplay, mouse controls, overlay shortcuts, correct music tempo and physical-controller quicksave/quickload were confirmed. Engine tests verified world/camera rollback, corrupt-state rejection, loading in a fresh process and recovery after explicitly recreating EGL. Rendering tests alone did not catch an earlier frontend synchronization issue, so physical RetroArch testing remains necessary.

Header-only tests are in `libretro/tests/{audio-clock,buttons,controller,save-state,simulation-clock}.cpp`. Build each with C++17 and include paths `libretro` and `libretro/include`; link `save-state.cpp` with zlib (`-lz`). `render.cpp` is the Android GLES integration harness; it requires user-provided game resources. Its optional environment flags include `RTTR_TEST_STATES=1`, `RTTR_TEST_CONTEXT_RESET=1`, `RTTR_TEST_FRAMES=1600`, `RTTR_TEST_STATE_INPUT` and `RTTR_TEST_STATE_OUTPUT`. `state-events.txt` drives the test-game setup. `RTTR_TEST_FAST_FORWARD=1` with at least 1400 frames measures actual gameframe progression at normal, 2x, 3x, normal again and unlimited speed. Run it separately from the state/context tests; its save operations interrupt audio measurement, so use a separate uninterrupted run to check audio rate.

RetroArch 1.20's Android controller-port mapping has a separate frontend bug when devices are reordered. It can affect remote buttons and menu navigation. This core does not fix RetroArch globally; see [upstream #19924](https://github.com/libretro/RetroArch/pull/19924). Do not treat frontend FPS alone as evidence of smooth gameplay.

The GLES harness also accepts `RTTR_TEST_RESET=1` together with `RTTR_TEST_STATES=1` and `RTTR_TEST_FRAMES=1600`. It first saves a live game, requires Reset to stop that game, and then verifies that loading restores the saved world and camera. Do not combine this flag with the context-loss test. Reset is deferred to `retro_run` so GL resource destruction happens with a current render context.

Publication check, 2026-10-29: the normal build completed and matched the installed artifact byte for byte (6,647,544 bytes, SHA-256 `2ecede0a118dadf0996abc1c55a7f1aca5b53d624bad77e730ed00b8bf835916`). All five header-level tests passed on ARM; the three upstream patches were verified. Before publication, the reset GLES regression failed with the old callback and passed after the fix, and actual RetroArch reset/load, fast-forward and Close Content were checked in gameplay.

## Credits and license

Based on [Return to the Roots](https://github.com/Return-To-The-Roots/s25client) and [s25rttr-android](https://github.com/Farmer-Markus/s25rttr-android), with SDL2, SDL_mixer, gl4es and their dependencies. See [THIRD_PARTY.md](THIRD_PARTY.md), the pinned upstream repositories and their license notices. Integration code is distributed under GPL-2.0-or-later; upstream files retain their own licenses. The Settlers II game data is not covered by this project's license.
