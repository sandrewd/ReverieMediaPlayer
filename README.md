# Reverie Media Player

A media player for Linux with Milkdrop-style visualisations built in — not running alongside in a
second window, but rendered into the same scene as the rest of the interface.

It exists because the available options sit at two extremes: players that look like they were
designed in 2009, and library-management applications that happen to play audio. Reverie is
neither. It plays what you give it, shows something worth looking at while it does, and does not
try to organise your music collection.

![Reverie playing a track with the visualiser running](docs/screenshot.png)

## What it does

- **Transport** — play, pause, stop, next, previous, seek, volume
- **Visualisations** — projectM (Milkdrop) presets, embedded or fullscreen, driven by the decoded
  audio. Right-click the visualiser to pick a preset by category, or leave it to rotate.
- **One playlist** — drag to reorder, multi-select, remove, save and load M3U. Not tabbed
  playlists, not a library.
- **Video** — plays video on the same surface the visualiser uses, letterboxed, with the controls
  over it
- **Internet radio** — streams are entries in the same playlist, with station metadata in the
  now-playing line
- **Mini-player** — a compact always-on-top layout
- **Themes** — follows the desktop's light/dark setting, with ten ready-made palettes and a
  four-colour custom editor
- **MPRIS** — media keys and desktop integration

**Deliberately not included:** library management, lyrics, social features, third-party service
integration.

## Adaptive visual quality

The visualiser renders to an offscreen buffer and upscales. On software rendering (llvmpipe) that
is the difference between usable and not: a heavy preset at 1280x720 runs around 10 fps at full
scale and around 20 fps at half. Reverie measures its own frame time and adjusts the render scale
to hold a target frame rate, stepping down quickly and back up slowly, because a visualiser whose
resolution visibly pumps is worse than one that is slightly soft.

You can override all of it in *Options > Visual quality*, including a fixed scale and the target
frame rate.

## Building

Requires Qt 6.4+, GStreamer 1.20+, TagLib, CMake 3.22+, and a C++17 compiler.

On Debian/Ubuntu (24.04 or newer):

```sh
sudo apt install build-essential cmake git \
    qt6-base-dev qt6-declarative-dev libqt6svg6-dev \
    qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
    qml6-module-qtquick-dialogs qml6-module-qtquick-window qml6-module-qtqml-workerscript \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav \
    libtag1-dev
```

**projectM is vendored and built alongside Reverie.** The version in Debian and Ubuntu is 2.1.0,
a 2012-era release, and is unusable. Reverie also depends on a projectM API that is not in any
tagged release yet: `projectm_opengl_render_frame_fbo()`. Every released projectM up to and
including 4.1.7 renders its final composite to framebuffer 0 regardless of what is bound, which
makes it impossible to draw into a Qt Quick scene graph — the item comes out black while still
costing the full frame time. So the submodule is pinned to a master commit.

```sh
git clone --recurse-submodules https://github.com/sandrewd/ReverieMediaPlayer.git
cd ReverieMediaPlayer

# Build the vendored projectM first
cmake -S vendor/projectm -B build/projectm-master \
      -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DENABLE_PLAYLIST=ON \
      -DCMAKE_INSTALL_PREFIX="$PWD/build/projectm-master-install"
cmake --build build/projectm-master -j"$(nproc)"
cmake --install build/projectm-master

# Then Reverie
cmake -S . -B build/player -DCMAKE_BUILD_TYPE=Release
cmake --build build/player -j"$(nproc)"
```

### Presets

The preset pack is **not** included in this repository — it is 157 MB and belongs to its own
project. Fetch it into `assets/presets/`:

```sh
git clone --depth 1 \
    https://github.com/projectM-visualizer/presets-cream-of-the-crop.git assets/presets
```

The build installs the 480 presets listed in `assets/presets-curated.txt`, roughly 5 MB, chosen
to spread across categories and to avoid the most expensive presets. Point `--presets` at any
directory to use a different set.

### Installing

```sh
sudo cmake --install build/player --prefix /usr/local
```

Or build a Debian package:

```sh
cmake --build build/player --target package
sudo apt install ./build/player/reverie_*_amd64.deb
```

## Status

Reverie is usable and in active development. Known gaps, stated plainly:

- **Wayland is untested.** It should work — nothing is architected against it — but the only
  platform it has been exercised on is X11. Always-on-top for the mini-player is X11-only and
  cannot be done portably on Wayland.
- **Subtitles, hardware decode and video track selection** are not implemented.
- **Performance figures assume software rendering.** On a GPU everything below is far better than
  the numbers above suggest.

## Licence

Reverie is released under the [MIT licence](LICENSE).

## Attributions

Reverie links dynamically against the following, all under their own licences:

| Project | Licence |
|---|---|
| [Qt 6](https://www.qt.io/) | LGPL-3.0 |
| [GStreamer](https://gstreamer.freedesktop.org/) | LGPL-2.1 |
| [projectM](https://github.com/projectM-visualizer/projectm) | LGPL-2.1, with bundled projectm-eval and hlslparser under MIT |
| [TagLib](https://taglib.org/) | LGPL-2.1 / MPL-1.1 |

The bundled presets are the Milkdrop **“Cream of the Crop”** collection, curated and sorted by
**ISOSCELES** and distributed with projectM. Per that collection's own notice, the presets were in
almost all cases never released under a specific licence and are treated as public domain, with
each preset author retaining copyright in their own work. If you are a preset author and do not
want your work included, the projectM team removes presets on request.

Milkdrop was created by Ryan Geiss. projectM is an independent reimplementation.
