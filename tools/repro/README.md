# Minimal reproductions: projectM renders nothing under Qt on some systems

Three programs that isolate a fault where projectM produces an entirely empty frame — no
GL error, no projectM warning, a complete framebuffer — on one machine while working on
another. They share the same projectM library, presets and API calls, and differ only in
how the OpenGL context is created.

| file | context | observed |
|---|---|---|
| `projectm-glx.cpp` | raw GLX, no Qt | renders on **both** machines |
| `projectm-qopenglwindow.cpp` | Qt, no Qt Quick | renders on one, **black** on the other |
| `projectm-qtquick.cpp` | Qt Quick `QQuickFramebufferObject` | renders on one, **black** on the other |

So the fault follows Qt's OpenGL context, not Qt Quick, not the GPU, and not projectM.

## Building

```sh
PM=../../build/projectm-master-install
g++ -O1 -o projectm-glx projectm-glx.cpp -I$PM/include -L$PM/lib -lprojectM-4 -lGL -lX11
g++ -std=c++17 -fPIC -O1 -o projectm-qopenglwindow projectm-qopenglwindow.cpp \
    $(pkg-config --cflags --libs Qt6OpenGL Qt6Gui Qt6Core) \
    -I$PM/include -L$PM/lib -lprojectM-4 -lprojectM-4-playlist
# projectm-qtquick.cpp additionally needs moc; see the session notes.
```

Each takes a preset directory and a duration in seconds, and prints the fraction of
non-black pixels read back from the render target every 30 frames.

## Ruled out

The GPU and driver (the fault reproduces with `LIBGL_ALWAYS_SOFTWARE`), projectM itself,
high-DPI scaling, framebuffer size, the Qt render loop (`basic` and `threaded` alike),
GL context identity between creation and render, framebuffer completeness, draw buffer,
scissor/viewport/colour mask, vertex array object binding, requested GL version, the
robust-access context flag, alpha buffer size, Qt's GLX-versus-EGL integration, Qt and
Mesa package versions, X visual depth, the preset playlist, and a screen-sharing daemon.
