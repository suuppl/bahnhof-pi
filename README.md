# Bahnhofstafel Pi

A Raspberry Pi departure board for any Deutsche Bahn station. Four cooperating C programs render live train departures and weather on the Pi's framebuffer (`/dev/fb0`) using LVGL.

```
iris_fetch    →  /run/bahnhofstafel/departures.json  ─┐
weather_fetch →  /run/bahnhofstafel/weather.json     ─┼→  bahnhofstafel (LVGL display)
weather_fetch →  /run/bahnhofstafel/bar.d/10-weather.json (sample bar plugin)
(your plugin  →  /run/bahnhofstafel/bar.d/NN-name.json)─┤
                                                      └→  brightness_ctl (HDMI backlight via DDC/CI)
```

## Display

1920×1080/1200, Dracula dark theme.

- **Header** — station name + clock (date + time, colon blinks)
- **Departure rows** — delayed trains show changed time in orange + planned time
- **Plugin bar** — filled entirely by plugins writing small JSON files; the bundled weather plugin shows temperature (min/max), UV index, rain probability and a condition icon

## Quick start

```sh
git clone --recursive <this repo>
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu cmake

# your station / coordinates (git-ignored)
cat > src/config_local.h <<'EOF'
#define STATION_NAME         "Hamburg Hbf"
#define OPEN_METEO_LAT       "53.55"
#define OPEN_METEO_LON       "10.01"
EOF

# your Pi (git-ignored)
echo 'PI := dietpi@raspberrypi.local' > local.mk

make            # cross-compile + deploy + restart
```

No API keys are required — DB IRIS and Open-Meteo are public.

## Configuration

| What | Where |
|------|-------|
| Station, coordinates, install dir, devices | `src/config_local.h` (overrides `src/config.h`) |
| Pi host, SSH key, extra daemons | `local.mk` or `make` variables |
| Poll intervals, brightness curve | `src/config.h` |
| JSON field names shared across programs | `src/schema.h` |
| Train line colours | `LINE_COLORS` in `src/config_local.h` |
| Bottom bar contents (plugins) | [`docs/plugins.md`](docs/plugins.md) |

## Hardware

- Raspberry Pi (aarch64, DietPi/Debian)
- HDMI monitor with DDC/CI brightness on `/dev/i2c-2` (optional)
- DFRobot C4001 mmWave presence sensor on UART (optional)

## Makefile targets

| Target | Action |
|--------|--------|
| `make` / `make all` | Cross-compile + deploy + restart |
| `make build` | Cross-compile only (into `build/`) |
| `make build-native` | Native build (compile check) |
| `make deploy` | Build, push binaries + service, restart |
| `make restart` | Restart service on Pi without rebuilding |
| `make clean` | Delete build dirs |

Logs on the Pi: `~/bahnhofstafel/log/*.log`

## Docs

- [`docs/index.md`](docs/index.md) — root file descriptions, directory map
- [`docs/src.md`](docs/src.md) — source file reference, data models, JSON shapes
- [`docs/plugins.md`](docs/plugins.md) — site config, bar plugins, embedding in a private repo
- [`docs/lvgl.md`](docs/lvgl.md) — LVGL submodule and `lv_conf.h` settings
- [`docs/sysroot.md`](docs/sysroot.md) — cross-compilation sysroot structure
- [`docs/build.md`](docs/build.md) — CMake build output
- [`docs/ssh.md`](docs/ssh.md) — SSH key setup
