# Bahnhofstafel — Project Overview

A Raspberry Pi departure board (Bahnhofstafel) for a Deutsche Bahn station of your choice.
A core (departure fetcher + display) and optional plugins render a live train/weather display on the Pi's framebuffer (`/dev/fb0`) via LVGL.

```
core      iris_fetch ─────→ departures.json ──────────→ bahnhofstafel (LVGL display)
                                                          ▲ bottom bar   ▲ blank/unblank
plugins   weather_fetch ──→ bar.d/10-weather.json ───────┘              │
                       └──→ weather.json (sunrise/sunset) ─→ brightness_ctl → monitor backlight (DDC/CI)
          presence_ctl ───→ screen.d/50-presence.json ───────────────────┘   (also pauses brightness_ctl)
          your plugin ────→ bar.d/NN-name.json  /  screen.d/NN-name.json
```
(all files under `/run/bahnhofstafel/`; `PLUGINS` in `bahnhofstafel.conf` picks which plugins start)

All JSON writes are atomic (`write_atomic()` in `src/fileio.c`: write to `{dest}.tmp.PID` then `rename()`), so readers never see a partial file.

---

## Root-level files

| File | Purpose |
|------|---------|
| [`CMakeLists.txt`](#cmakeliststxt) | CMake build definition for all four binaries |
| [`Makefile`](#makefile) | Convenience wrapper: build + cross-compile + deploy to Pi |
| [`lv_conf.h`](#lv_confh) | LVGL compile-time configuration (FreeType, FS POSIX, …) |
| [`aarch64-toolchain.cmake`](#aarch64-toolchaincmake) | CMake cross-compilation toolchain for Raspberry Pi (aarch64) |
| [`.env.example`](#envexample) | Template for the secrets file (`.env` is gitignored) |

---

### `CMakeLists.txt`

Defines four executables plus the `bahnhof_common` helper library (for out-of-tree fetchers):

- **`iris_fetch`** — links `libcurl`, `libxml2`, `libcjson`, `libm`; sources include shared `http.c`, `fileio.c`
- **`weather_fetch`** — links `libcurl`, `libcjson`; sources include shared `http.c`, `fileio.c`
- **`brightness_ctl`** (plugin) — links `libcjson`; sources include `brightness_curve.c`, `screen.c`
- **`presence_ctl`** (plugin) — links `libcjson`; sources include `screen.c`
- **`bahnhofstafel`** — links `lvgl` (as CMake subdirectory), `libcjson`, `libfreetype`, `libm`

Has two build paths controlled by whether `CROSS_LIB_DIR` is set (injected by the toolchain file):

- **Cross-compile** (`CROSS_LIB_DIR` set): searches libraries in `sysroot/lib/aarch64-linux-gnu`, skips `find_package`.
- **Native**: uses `find_package(CURL)`, `find_package(LibXml2)`, `pkg-config` fallback.

---

### `Makefile`

High-level developer workflow wrapper. Targets:

| Target | What it does |
|--------|-------------|
| `make` / `make all` | Build then deploy |
| `make build` | Cross-compile all four binaries into `build/` |
| `make deploy` | Stop service on Pi, push binaries + `start.sh` + service file, restart |
| `make restart` | Restart the systemd service on Pi without rebuilding |
| `make clean` | Delete `build/` |

Binary transfer uses `cat > file.new && mv file.new file` to avoid "text file busy" errors. The `bahnhofstafel` binary is gzip-streamed to avoid SSH timeout on slow connections.

SSH connection variables (`PI`, `SSH`) are defined at the top of the file.

---

### `lv_conf.h`

LVGL's single-header configuration file. Notable settings:

- **`LV_USE_FS_POSIX 1`** with drive letter `'A'` — exposes the filesystem to LVGL under the prefix `A:/`. Required for FreeType to load font files from disk.
- **`LV_USE_FREETYPE 1`** — enables runtime TTF rendering via FreeType so any Unicode glyph (umlauts, weather symbols, arrows, emoji) renders correctly.
- Display color depth, resolution, and framebuffer driver are also configured here.

---

### `aarch64-toolchain.cmake`

CMake toolchain file for cross-compiling on an x86-64 host targeting the Pi's aarch64 Debian/DietPi system.

- Sets compiler to `aarch64-linux-gnu-gcc` / `aarch64-linux-gnu-g++`.
- Points `CMAKE_FIND_ROOT_PATH` at `./sysroot/` so `find_library()` resolves Pi headers and `.so` stubs.
- Sets `CROSS_INCLUDE_DIRS` and `CROSS_LIB_DIR` cache variables consumed by `CMakeLists.txt`.
- Uses `-Wl,--allow-shlib-undefined` because transitive shared-library dependencies exist on the Pi but not in the local sysroot.

Activate with: `cmake -B build -DCMAKE_TOOLCHAIN_FILE=aarch64-toolchain.cmake`

---

### `.env.example`

Placeholder for a future local secrets file. Currently unused — no daemon requires an API key or secret (`weather_fetch` polls the free Open-Meteo forecast API). `start.sh` still sources `~/bahnhofstafel/.env` on the Pi if present, so this remains available for any secret added later.

---

## Subdirectories

| Directory | Contents | Docs |
|-----------|----------|------|
| `src/` | C source code for all four programs | [src.md](src.md) |
| `systemd/` | Service unit, `start.sh`, hotkey config | — |
| `fonts/` | Runtime fonts deployed to Pi (`DejaVuSans`, `Twemoji.Mozilla.ttf`) | — |
| `lvgl/` | LVGL graphics library (git submodule, v9.x) | [lvgl.md](lvgl.md) |
| `sysroot/` | Raspberry Pi headers + library stubs for cross-compilation | [sysroot.md](sysroot.md) |
| `build/` | CMake build output (generated, not in version control) | [build.md](build.md) |
| `.ssh/` | SSH key for Pi access (not in version control) | [ssh.md](ssh.md) |

---

### `systemd/`

| File | Purpose |
|------|---------|
| `bahnhofstafel.service` | systemd unit (oneshot + RemainAfterExit, autostarts on boot) |
| `start.sh` | Launch script: sources `.env`, kills old processes, starts `iris_fetch`, the display and the `PLUGINS` from `bahnhofstafel.conf` (default: bundled plugins + `extra_daemons`) |
| `bahnhofstafel-hotkeys.conf` | triggerhappy config: Ctrl+Alt+S stops, Ctrl+Alt+R restarts |
| `triggerhappy-override.conf` | systemd drop-in to allow triggerhappy to call `systemctl` |

---

### `fonts/`

| File | Purpose |
|------|---------|
| `DejaVuSans.ttf` / `DejaVuSans-Bold.ttf` | Primary text font (system copy used on Pi; not bundled) |
| `Twemoji.Mozilla.ttf` | Color emoji font (COLR v0 format); deployed to `~/bahnhofstafel/fonts/` |
| `NotoEmoji-Regular.ttf` | Monochrome emoji fallback |
