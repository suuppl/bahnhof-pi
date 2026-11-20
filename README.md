# Bahnhofstafel Pi

A Raspberry Pi departure board for any Deutsche Bahn station. A small core (departure fetcher + display) and optional plugins render live train departures, weather and more on the Pi's framebuffer (`/dev/fb0`) using LVGL.

```
core      iris_fetch ─────→ departures.json ──────────→ bahnhofstafel (LVGL display)
                                                          ▲ bottom bar   ▲ blank/unblank
plugins   weather_fetch ──→ bar.d/10-weather.json ───────┘              │
                       └──→ weather.json (sunrise/sunset) ─→ brightness_ctl → monitor backlight (DDC/CI)
          presence_ctl ───→ screen.d/50-presence.json ───────────────────┘   (also pauses brightness_ctl)
          your plugin ────→ bar.d/NN-name.json  /  screen.d/NN-name.json
```
(all files under `/run/bahnhofstafel/`; `PLUGINS` in `bahnhofstafel.conf` picks which plugins start)

## Display

1920×1080/1200, Dracula dark theme.

- **Header** — station name + clock (date + time, colon blinks)
- **Departure rows** — delayed trains show changed time in orange + planned time; platform column (changed platform in orange, `SHOW_PLATFORM=0` hides it)
- **Plugin bar** — filled entirely by plugins writing small JSON files; the bundled weather plugin shows temperature (min/max), UV index, rain probability and a condition icon

## Quick start

```sh
git clone --recursive <this repo>
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu cmake

# your station / coordinates (runtime settings, git-ignored)
cp bahnhofstafel.conf.example bahnhofstafel.conf
#   STATION_NAME="Hamburg Hbf"  OPEN_METEO_LAT=53.55  OPEN_METEO_LON=10.01  ...

# your Pi (git-ignored)
echo 'PI := dietpi@raspberrypi.local' > local.mk

make            # cross-compile + deploy + restart
```

No API keys are required — DB IRIS and Open-Meteo are public.

## Configuration

| What | Where |
|------|-------|
| Station, train filter, delay threshold, platform column (`SHOW_PLATFORM`), coordinates, intervals, brightness, devices, fonts | `bahnhofstafel.conf` (runtime, restart to apply) — all keys in [`bahnhofstafel.conf.example`](bahnhofstafel.conf.example); defaults in `src/config.h` |
| Pi host, SSH key, extra daemons | `local.mk` or `make` variables |
| JSON field names shared across programs | `src/schema.h` |
| Train line colours | `line_colors.json` → [`docs/line-colors.md`](docs/line-colors.md) |
| Plugins to start (`PLUGINS`) | `bahnhofstafel.conf` |
| Bottom bar / screen plugins | [`docs/plugins.md`](docs/plugins.md) |

## Hardware

- Raspberry Pi (aarch64, DietPi/Debian)
- HDMI monitor with DDC/CI brightness on `/dev/i2c-2` (optional, `brightness_ctl` plugin)
- DFRobot C4001 mmWave presence sensor on UART (optional, `presence_ctl` plugin)

## Makefile targets

| Target | Action |
|--------|--------|
| `make` / `make all` | Cross-compile + deploy + restart |
| `make build` | Cross-compile only (into `build/`) |
| `make build-native` | Native build (compile check) |
| `make deploy` | Build, push binaries + service, restart |
| `make config` | Push only `bahnhofstafel.conf` and restart |
| `make colors` | Push only `line_colors.json` (applied live) |
| `make restart` | Restart service on Pi without rebuilding |
| `make clean` | Delete build dirs |

Logs on the Pi: `~/bahnhofstafel/log/*.log`

## Docs

- [`docs/index.md`](docs/index.md) — root file descriptions, directory map
- [`docs/src.md`](docs/src.md) — source file reference, data models, JSON shapes
- [`docs/plugins.md`](docs/plugins.md) — site config, bar plugins, embedding in a private repo
- [`docs/line-colors.md`](docs/line-colors.md) — train line colours
- [`docs/lvgl.md`](docs/lvgl.md) — LVGL submodule and `lv_conf.h` settings
- [`docs/sysroot.md`](docs/sysroot.md) — cross-compilation sysroot structure
- [`docs/build.md`](docs/build.md) — CMake build output
- [`docs/ssh.md`](docs/ssh.md) — SSH key setup
