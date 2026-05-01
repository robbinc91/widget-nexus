# Widget Nexus — Linux build (`linx-dst`)

GTK **3** port aimed at **Ubuntu** (and similar desktops). It reads and writes the same **`widgets.txt`** format as the Win32 project in the repository root.

## Install dependencies (Ubuntu)

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libgtk-3-dev libayatana-appindicator3-dev
```

`libayatana-appindicator3-dev` is optional but recommended: **Hide** minimizes to the **system tray** (notification area). If CMake does not find Ayatana, the binary still builds; **Hide** only hides the window (use Alt+Tab or process manager to find it again unless you add tray support later).

## Configure & build

```bash
cd linx-dst
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run:

```bash
./build/widget-nexus-lnx
```

`widgets.txt` is resolved next to the **executable** (via `/proc/self/exe`), same idea as the Windows build.

## Behaviour notes vs Windows

- Commands run under **`/bin/sh -c`**. Lines containing **`ssh`**, **`vim`**, **`nano`**, **`top`**, etc. are treated as interactive and launched with **`$TERMINAL`** or **`x-terminal-emulator`** / **`gnome-terminal`** as fallback.
- Circular floaters use **RGBA** windows and **shape** masks; exact compositing depends on your Wayland/X11 session and window manager.
- **Pinned** widgets map to **`gtk_window_set_keep_above`** (always-on-top hint).

## Files

| Path | Role |
|------|------|
| `src/main.cpp` | GTK UI, floaters, tray, layout |
| `src/model.hpp`, `src/model.cpp` | Config I/O + command runner |
| `CMakeLists.txt` | Build |
