# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Architecture

A core (`iris_fetch` + display) and optional plugin daemons run on the Pi and communicate via JSON files on tmpfs:

```
core      iris_fetch ─────→ departures.json ──────────→ bahnhofstafel (LVGL display)
                                                          ▲ bottom bar   ▲ blank/unblank
plugins   weather_fetch ──→ bar.d/10-weather.json ───────┘              │
                       └──→ weather.json (sunrise/sunset) ─→ brightness_ctl → monitor backlight (DDC/CI)
          presence_ctl ───→ screen.d/50-presence.json ───────────────────┘   (also pauses brightness_ctl)
          your plugin ────→ bar.d/NN-name.json  /  screen.d/NN-name.json
```
(all files under `/run/bahnhofstafel/`; `PLUGINS` in `bahnhofstafel.conf` picks which plugins start)

- **`iris_fetch`** — polls DB IRIS-TTS XML API every 60 s; writes departure data
- **`weather_fetch`** — polls the Open-Meteo forecast API (free, keyless JSON) every 600 s; writes `weather.json` (incl. `sunrise`/`sunset`, for `brightness_ctl`) and is the **sample bar plugin** (`bar.d/10-weather.json`)
- **`bahnhofstafel`** — LVGL framebuffer display; renders departures (`DEP_POLL_MS`) and the bottom bar from all `bar.d/*.json` plugin files (`BAR_POLL_MS`); knows nothing about weather; never returns
- **`brightness_ctl`** (plugin) — reads `weather.json` sunrise/sunset and sets the monitor backlight via DDC/CI (raw I²C write, VCP 0x10) on a smooth day/night curve; pauses while the screen is off. See "Brightness" below.
- **`presence_ctl`** (plugin) — mmWave radar on UART → `screen.d/50-presence.json` (`{"on": false}` after `PRESENCE_TIMEOUT_S` without presence). The display blanks the framebuffer whenever any `screen.d/*.json` says off (`screen.c`).
- Which plugins start: `PLUGINS` in `bahnhofstafel.conf` (read by `start.sh`; default = bundled + `extra_daemons`).

All JSON writes are atomic (`write_atomic()` in `src/fileio.c`: write to `{dest}.tmp.PID`, then `rename()`) so the display never reads a partial file. `iris_fetch` and `weather_fetch` share `http.c` (libcurl GET helper) and `fileio.c`.

Two shared headers, `#include`d by all programs, hold no logic — only `#define` constants:
- **`config.h`** — URLs, IPC paths, poll intervals, limits, brightness tuning
- **`schema.h`** — JSON field name constants (`DEP_*`, `WEA_*`, `BAR_*`) for the IPC files, so field renames propagate at compile time

Source files in `src/`: `config.h`, `schema.h`, `http.c/h`, `fileio.c/h`, `iris_fetch.c`, `weather_fetch.c`, `brightness_curve.c/h`, `brightness_ctl.c`, `presence_ctl.c`, `screen.c/h`, `settings.c/h`, `main.c`, `display.c/h`.

### Brightness (DDC/CI over HDMI I²C)

The HDMI monitor has no `/sys/class/backlight`; brightness is the monitor's own backlight, set over the DDC bus `/dev/i2c-2` (addr `0x37`, VCP `0x10`). **Writes are raw and never read back** — the vc4 HDMI I²C read channel is unreliable (ddcutil's verify-read fails ~80-100% of the time), but the SET writes apply fine. `brightness_ctl` runs as the service user with the `i2c` supplementary group (granted by the systemd unit). The day/night ramp itself is a pure function in `brightness_curve.c` (sigmoid, half-width `BRIGHT_RAMP_S`); tune levels/ramp in `config.h` (`BRIGHT_*`).

## Build and deploy

```sh
make          # cross-compile all binaries + deploy to Pi + restart
make sysroot  # copy the cross-compile headers/libs from the Pi (once / after upgrades)
make build    # cross-compile only (into build/)
make deploy   # build, push binaries and restart
make restart  # restart bahnhofstafel.service on Pi without rebuilding
make clean    # delete build/ and build-native/
```

Cross-compilation uses `aarch64-toolchain.cmake` with `aarch64-linux-gnu-gcc` and headers/libraries from `sysroot/` (git-ignored; `make sysroot` copies libcurl/libxml2/libcjson/libfreetype from the Pi over SSH, `make build` does it if missing; see `docs/sysroot.md`). `CMakeLists.txt` also supports a **native build** (omit the toolchain flag) using system `find_package`/`pkg-config` — useful for checking compilation errors without the Pi:

```sh
cmake -B build-native && cmake --build build-native -j$(nproc)
```

`make deploy` transfers binaries via `cat > file.new && mv file.new file` (avoids "text file busy"); the `bahnhofstafel` binary is gzip-streamed to avoid SSH timeouts. Fonts are pushed only if missing on the Pi.

### Docker

`Dockerfile` (stages `plugins` → `sdk` → `runtime`) builds everything natively inside Debian, meant to be built on the Pi by compose straight from this git repo at a pinned full commit hash (no registry, no CI). `docker/entrypoint.sh` starts iris_fetch + display + plugins (default: every other binary in `/app/bin`), exits when a core process dies. Out-of-tree single-file plugins: `plugins` build context (compose `additional_contexts`) → `BAHNHOF_PLUGIN_DIR` (CMake). See `docs/docker.md`.

### Secrets

None currently required — `weather_fetch` polls Open-Meteo's free public API, no API key. `.env`/`start.sh`'s `.env` sourcing is kept as a placeholder for any future secret.

## Pi access

```sh
ssh -i .ssh/pi_key $PI   # PI is set in local.mk
```

Log files on Pi: `~/bahnhofstafel/log/` (`iris.log`, `display.log`, `<plugin>.log`)

## Runtime (systemd) on the Pi

All processes run under **`bahnhofstafel.service`** (oneshot launcher of `start.sh`, `RemainAfterExit`, autostarts on boot). `make deploy`/`make restart` drive it via `sudo systemctl`. Unit + hotkey configs are versioned in `systemd/`.

- **Hotkeys** (global, via `triggerhappy` running as root): **Ctrl+Alt+S** stops the display, **Ctrl+Alt+R** restarts it. Triggers in `/etc/triggerhappy/triggers.d/bahnhofstafel.conf`; trigger key comes first, modifiers appended (`KEY_S+KEY_LEFTCTRL+KEY_LEFTALT`).
- **`/run/bahnhofstafel`** (tmpfs IPC dir) is recreated each boot by `/etc/tmpfiles.d/bahnhofstafel.conf` — `start.sh` runs as non-root and can't create it under `/run`.
- Non-`sudo` `systemctl` queries fail over SSH here ("Failed to connect to system scope bus"); always use `sudo systemctl`.

## Key facts

- Target: Raspberry Pi aarch64, DietPi/Debian, framebuffer `/dev/fb0`
- Station: `STATION_NAME` (example default Berlin Hbf); EVA resolved in `iris_fetch.c`: live lookup by name → `EVA_CACHE` file → optional `STATION_EVA_FALLBACK` (default 0); retried every cycle while unresolved
- IRIS fetch order: **`fchg` first, then `rchg`** — `fchg` contains early-announced cancellations; `rchg` is merged after so its newer data overrides `fchg` for the same trip ID
- Weather: Open-Meteo forecast API (`api.open-meteo.com`), no API key, no Visual Crossing/meteoblue despite older docs. Single request with `timezone=auto` returns `sunrise`/`sunset`/hourly timestamps as plain local ISO8601 strings — `weather_fetch.c` parses them with `sscanf` + `mktime()` (no UTC-offset math needed, relying on the Pi's system TZ matching the station's location); `weather.json`'s `sunrise`/`sunset` (real epoch seconds) are consumed by `brightness_ctl`. Condition icon comes from Open-Meteo's WMO `weather_code` + `is_day`, mapped in `weather_fetch.c` to a small fixed vocabulary `display.c` renders as an emoji glyph.
- Bottom bar = plugins only: any process writes `bar.d/NN-name.json` (groups of emoji/value/width/sub, big `icon`, `align`, `dim`), concatenated in filename order. Site config is runtime: `bahnhofstafel.conf` (KEY=value, key = `config.h` macro name, env overrides file; `settings.c`, `CFG_S()`/`CFG_I()` in code). `config.h` only holds defaults. See `docs/plugins.md`.
- **Never commit site-specific data** (station name/EVA, coordinates, local line names/colours, IPs, keys) — this repo is public; use `bahnhofstafel.conf` / `line_colors.json` / `local.mk` (all git-ignored).
- Train filter: `TRAIN_FILTER` (runtime setting) applied in `iris_fetch.c` `write_departures()`; letters = type, digits = train number, mixed = exact label.
- Train pill colours: runtime `APP_DIR/line_colors.json` (`"S1": "#rrggbb"` or `{bg,fg}`), lookup exact label → type (leading letters) → built-in type default → grey; reloaded on change (`docs/line-colors.md`). Train label in `iris_fetch.c` `make_train_label()`: IRIS `dp@l` may be `"S3"`, `"8"`, `"RE"` or empty.
- Fonts: DejaVu Sans + Twemoji/NotoEmoji via FreeType (`LV_USE_FREETYPE`) at runtime; `lv_conf.h` sets `LV_USE_FS_POSIX` (drive `A:` = `/`) so font files load from disk

## Implementation gotchas

- **cJSON memory**: `cJSON_GetObjectItem` returns pointers into the tree owned by `cJSON_Delete`. Copy string values (`description`, `icon`) into stack buffers *before* calling `cJSON_Delete`, or you get use-after-free.
- **IRIS time parsing**: timestamps are in local German time (`YYMMddHHmm`), parsed via `mktime()` which respects `TZ`. The Pi must have the correct timezone set.
- **Hidden stops**: departures with `dp@hi="1"` are operator-hidden and must be filtered from the output JSON.
- **`→` arrow**: destination cells use U+2192 as UTF-8 `\xe2\x86\x92`; FreeType renders this from DejaVu Sans.

## Full documentation

- [`docs/index.md`](docs/index.md) — root-level file descriptions, directory map
- [`docs/src.md`](docs/src.md) — detailed data models, fetch cycles, JSON shapes for all source files
- [`docs/plugins.md`](docs/plugins.md) — runtime settings, bar plugins, embedding in a private repo
- [`docs/line-colors.md`](docs/line-colors.md) — train line colours
- [`docs/lvgl.md`](docs/lvgl.md) — LVGL submodule and `lv_conf.h` settings
- [`docs/sysroot.md`](docs/sysroot.md) — cross-compilation sysroot structure
- [`docs/build.md`](docs/build.md) — CMake build output
- [`docs/ssh.md`](docs/ssh.md) — SSH key setup
