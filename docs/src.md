# `src/` — Source Code

Eleven C files and three headers implement the four programs.

| File | Belongs to | Role |
|------|-----------|------|
| [`config.h`](#configh) | all four | Shared compile-time constants (URLs, paths, intervals, brightness) |
| [`schema.h`](#schemah) | all four | JSON field name constants for the IPC files |
| [`http.h`/`http.c`](#httpc) | `iris_fetch`, `weather_fetch` (+ extra fetchers via `bahnhof_common`) | Shared libcurl HTTP GET helper |
| [`fileio.h`/`fileio.c`](#fileioc) | `iris_fetch`, `weather_fetch` (+ extra fetchers via `bahnhof_common`) | Atomic file write and copy helpers |
| [`iris_fetch.c`](#iris_fetchc) | `iris_fetch` | Polls DB IRIS-TTS API; writes `departures.json` |
| [`weather_fetch.c`](#weather_fetchc) | `weather_fetch` | Polls Open-Meteo forecast API; writes `weather.json` |
| [`brightness_curve.h`/`brightness_curve.c`](#brightness_curvec) | `brightness_ctl` | Pure brightness ramp function (sigmoid day/night curve) |
| [`brightness_ctl.c`](#brightness_ctlc) | `brightness_ctl` | Reads sunrise/sunset; sets HDMI backlight via DDC/CI I²C |
| [`main.c`](#mainc) | `bahnhofstafel` | LVGL init + framebuffer setup; entry point |
| [`display.c`](#displayc) | `bahnhofstafel` | All rendering logic: layout, fonts, data polling |
| [`display.h`](#displayh) | `bahnhofstafel` | Public interface for `display.c` (two functions) |

---

## `config.h`

Single shared header `#include`d by all four programs. Contains only `#define` constants — no types or functions.

**Station**
Every site-specific value is `#ifndef`-guarded and can be overridden from an optional `config_local.h` (see [`plugins.md`](plugins.md)).

- `STATION_NAME` — display name / IRIS lookup name (default `"Berlin Hbf"`)
- `STATION_EVA_FALLBACK` — optional fallback EVA number (default 0 = none) used only if the live lookup fails and there is no cache yet; `iris_fetch` keeps retrying the lookup while unresolved
- `LINE_COLORS` — `{ prefix, bg, fg }` table for the train pills (first prefix match wins; default: generic S-Bahn green / regional red / long-distance white)

**Weather (Open-Meteo forecast API)**
- `OPEN_METEO_URL` — API base URL (`https://api.open-meteo.com/v1/forecast`)
- `OPEN_METEO_LAT` / `OPEN_METEO_LON` — station coordinates (default Berlin) — no API key needed, free public endpoint

**IRIS API base URLs**
- `IRIS_STATION_URL` — station name → EVA number lookup
- `IRIS_PLAN_URL` — timetable plan per station/date/hour
- `IRIS_RCHG_URL` — recent changes (last ~60 min of updates)
- `IRIS_FCHG_URL` — full changes (early-announced cancellations)

**IPC paths**
- `DATA_DIR` — `/run/bahnhofstafel` (tmpfs; recreated each boot by `tmpfiles.d`)
- `DEPARTURES_JSON` — `DATA_DIR/departures.json`
- `WEATHER_JSON` — `DATA_DIR/weather.json`
- `BAR_DIR` — `DATA_DIR/bar.d` (bottom-bar plugin files, see [`plugins.md`](plugins.md)); `BAR_WEATHER_JSON` = `BAR_DIR/10-weather.json`
- `WEATHER_CACHE` — `APP_DIR/cache/weather.json` (persistent, survives reboots)
- `EVA_CACHE` — `APP_DIR/cache/eva` (cached EVA number)

**Poll intervals**
- `IRIS_INTERVAL_S` (60 s) — how often `iris_fetch` polls
- `WEATHER_INTERVAL_S` (600 s) — how often `weather_fetch` polls
- `DEP_POLL_MS` (5000 ms) — how often `display.c` re-reads `departures.json`
- `BAR_POLL_MS` (1000 ms) — how often `display.c` rescans `bar.d/`

**Limits**
- `MAX_DEPARTURES` (120) — size of the in-memory departure array in `iris_fetch`
- `MAX_ROWS` (16) — maximum rows the display can show (actual count set at runtime from screen height)

**Brightness**
- `BRIGHT_DAY_PCT` / `BRIGHT_NIGHT_PCT` — daytime/nighttime backlight levels (%)
- `BRIGHT_RAMP_S` (2700) — half-width of the dawn/dusk transition (45 min)
- `BRIGHT_INTERVAL_S` (30) — how often `brightness_ctl` recomputes and writes
- `BRIGHT_FALLBACK_RISE_H` / `BRIGHT_FALLBACK_SET_H` — fallback sunrise/sunset hours when `weather.json` is unavailable

**DDC/CI**
- `I2C_BUS_PATH` — `/dev/i2c-2` (HDMI DDC bus on this Pi)
- `DDC_I2C_ADDR` — `0x37`
- `VCP_BRIGHTNESS` — `0x10` (MCCS backlight feature code)

---

## `schema.h`

Shared header defining `#define` constants for every JSON field name in the IPC files. Included by all programs so field renames propagate at compile time.

- **`DEP_*`** — fields in `departures.json` (`DEP_PLANNED_TIME`, `DEP_TRAIN`, `DEP_DELAY_MIN`, …)
- **`WEA_*`** — fields in `weather.json` (`WEA_TEMP`, `WEA_TEMPMIN`, `WEA_TEMPMAX`, `WEA_UVI`, `WEA_MAX_UVI`, `WEA_PRECIPPROB`, `WEA_SUNRISE`, `WEA_SUNSET`, `WEA_ICON`)
- **`BAR_*`** — fields in the `bar.d/*.json` plugin files (`BAR_GROUPS`, `BAR_EMOJI`, `BAR_VALUE`, `BAR_WIDTH`, `BAR_SUB`, `BAR_ARROW`, `BAR_BIG_ICON`, `BAR_ALIGN`, `BAR_DIM`)

---

## `http.c`

Shared libcurl HTTP GET helper used by `iris_fetch`, `weather_fetch`, and any extra fetcher.

```c
char *http_get(const char *url);
```

Returns a `malloc`'d NUL-terminated response body on success (HTTP 2xx), `NULL` on any error. Caller must `free()`. Sets a 20 s timeout, follows redirects. Logs errors to stderr.

---

## `fileio.c`

Shared atomic file I/O helpers.

```c
int write_atomic(const char *dest, const char *content);
int copy_atomic(const char *src, const char *dst);
```

Both write to a `{dest}.tmp.{pid}` temp file then `rename()` to the destination. `rename()` is atomic on POSIX, so readers never see a partial write.

---

## `iris_fetch.c`

Standalone daemon. Polls the DB IRIS-TTS API once per minute and atomically writes `/run/bahnhofstafel/departures.json`.

### Data model

```c
typedef struct {
    char id[128];              // IRIS stop ID (unique per trip/station)
    char category[16];         // e.g. "S", "ICE", "RE"
    char number[16];           // trip number
    char line[16];             // S-Bahn line designation (e.g. "S1")
    time_t planned_ts;         // scheduled departure (unix)
    time_t changed_ts;         // revised departure, 0 if unchanged
    char planned_platform[16];
    char changed_platform[16];
    char destination[192];     // last stop in the route
    char status;               // 'p'=planned, 'a'=added, 'c'=cancelled
    int  delay_min;            // (changed_ts - planned_ts) / 60
    int  hidden;               // dp@hi flag: operator-hidden stop
} Departure;
```

### EVA resolution (three-tier)

1. **Live lookup** — queries `IRIS_STATION_URL/<STATION_NAME>` at startup; saves result to `EVA_CACHE`.
2. **File cache** — loads `EVA_CACHE` immediately at startup so fetching begins before the live lookup returns.
3. **Fallback** — `STATION_EVA_FALLBACK` if set (non-zero); otherwise the lookup is retried every cycle until it succeeds.

### Fetch cycle (every 60 s)

1. **Plan fetch** — requests hourly plan slots for `delta = -1` to `+10` hours from now.
2. **Sort** — departures sorted ascending by `planned_ts`.
3. **Full changes (`fchg`)** — fetched first; contains early-announced cancellations.
4. **Recent changes (`rchg`)** — merged after `fchg` so newer data overrides for the same trip.
5. **Write JSON** — filters hidden stops and entries more than 2 min past (3 min for cancelled).

### Output JSON shape

```json
{
  "fetched_at": 1750000000,
  "station": "Berlin Hbf",
  "eva": 8011160,
  "departures": [
    {
      "planned_time": "14:31",
      "changed_time": "",
      "train": "S1",
      "destination": "Hauptbahnhof",
      "planned_platform": "2",
      "changed_platform": "",
      "delay_min": 0,
      "status": "p"
    }
  ]
}
```

`status` values: `"p"` planned, `"c"` cancelled, `"a"` additional service.

---

## `weather_fetch.c`

Standalone daemon. Polls the free, keyless Open-Meteo forecast API (`api.open-meteo.com/v1/forecast`) every 10 minutes and atomically writes `/run/bahnhofstafel/weather.json`. Also maintains a persistent cache at `WEATHER_CACHE` so the display has data immediately after a reboot.

### Fetch cycle (every 600 s) — single GET

One request built from `OPEN_METEO_URL`/`OPEN_METEO_LAT`/`OPEN_METEO_LON`, with `daily=sunrise,sunset,uv_index_max,temperature_2m_max,temperature_2m_min,precipitation_probability_max`, `current=temperature_2m,weather_code,is_day,uv_index`, `timezone=auto`, `forecast_days=1`. Every requested field maps to something `display.c`/`brightness_ctl.c` actually consumes — no unused fields are fetched.

- `temp` = `current.temperature_2m`; `uvi` = `current.uv_index` (a genuine live reading); `tempmin`/`tempmax` = `daily.temperature_2m_min[0]`/`daily.temperature_2m_max[0]`; `max_uvi` = `daily.uv_index_max[0]`; `precipprob` = `daily.precipitation_probability_max[0]` (the day's peak hourly probability — the conventional "chance of rain today" metric; Open-Meteo also offers `_mean`/`_min` variants, not used here).
- **Model note**: Open-Meteo's default "best_match" blends providers per variable rather than using one model for everything — for this station it resolves to DWD ICON for temperature, but UV comes from a different provider since DWD ICON doesn't compute UV at all (confirmed: `models=dwd_icon_seamless` returns `null` for `uv_index`/`uv_index_max`, while omitting `models=` — as this code does — returns real values). No `models=` param is passed, so all variables get a live/complete reading.
- `sunrise`/`sunset` = `daily.sunrise[0]`/`daily.sunset[0]`, ISO8601 **local** strings (e.g. `"2026-07-04T05:20"`) since the request uses `timezone=auto`. Parsed via `sscanf("%d-%d-%dT%d:%d")` + `mktime()` into genuine epoch seconds — no UTC-offset math needed, since the Pi's system TZ already matches this station's one fixed location (same assumption as the IRIS time-parsing gotcha above).
- **Condition icon**: Open-Meteo's WMO `weather_code` plus `is_day` are mapped by a local `wmo_to_icon()` function — an explicit `switch` covering every code in the official "WMO Weather interpretation codes (WW)" table (open-meteo.com/en/docs), not a range approximation — to the same small vocabulary (`clear-day`, `clear-night`, `cloudy`, `partly-cloudy-day`, `partly-cloudy-night`, `drizzle`, `rain`, `showers`, `sleet`, `snow`, `snow-grains`, `snow-showers`, `thunderstorm`, `thunderstorm-hail`, `fog`) that `display.c`'s `icon_to_symbol()` renders as an emoji glyph. See the full code table below.
- No retries; a GET failure, JSON parse failure, or a missing `current`/`daily` object aborts the cycle (logged to stderr) and the next attempt happens on the next 600 s tick. Individual missing/null leaf fields degrade to `0` via the existing null-safe `cJSON_GetNumberValue` pattern rather than aborting.

### Output JSON shape

```json
{
  "fetched_at": 1750000000,
  "temp": 22.9,
  "tempmin": 11.9,
  "tempmax": 25.5,
  "uvi": 6.5,
  "max_uvi": 7.05,
  "precipprob": 0.0,
  "sunrise": 1750030000,
  "sunset":  1750085000,
  "icon": "clear-day"
}
```

`sunrise`/`sunset` are Unix timestamps used by `brightness_ctl`. `precipprob` is the day's peak chance of precipitation as a percentage (0-100), rendered by `display.c` with a `%` suffix. `icon` is mapped to a Unicode symbol by `display.c`'s `icon_to_symbol()`.

---


## `brightness_curve.c`

Pure function module — no I/O, no side effects.

```c
int brightness_curve(time_t now, time_t sunrise, time_t sunset);
```

Returns target brightness percent (0–100). Uses a sigmoid (`smoothstep`) ramp of half-width `BRIGHT_RAMP_S` (45 min) centred on `sunrise` and `sunset`.

---

## `brightness_ctl.c`

Standalone daemon. Reads `sunrise`/`sunset` from `weather.json`, calls `brightness_curve()`, and writes the result to the HDMI monitor's backlight via a raw DDC/CI Set-VCP-Feature I²C frame.

- Falls back to `fallback_sun_times()` (from `BRIGHT_FALLBACK_RISE_H`/`SET_H`) if `weather.json` is unavailable — the full sigmoid curve is used with configurable clock hours, so brightness still ramps smoothly without weather data.
- Writes are raw and never read back — the vc4 HDMI I²C read channel is unreliable on this Pi.
- Runs as `dietpi` with the `i2c` supplementary group (granted by the systemd unit).
- Re-checks every `BRIGHT_INTERVAL_S` seconds; only issues an I²C write when the target level changes.

---

## `main.c`

Entry point for the `bahnhofstafel` display binary.

1. Calls `hide_console_cursor()` — reads `/sys/class/tty/tty0/active`, opens that TTY, sends `\033[?25l` and `ioctl(KDSETMODE, KD_GRAPHICS)` to suppress the fbcon cursor.
2. Calls `lv_init()` and registers a monotonic tick source.
3. Creates an LVGL framebuffer display targeting `/dev/fb0`.
4. Calls `display_init()` then `display_run()` (never returns).

---

## `display.c`

All LVGL rendering. Reads JSON files written by the fetcher daemons and updates the screen.

### Layout (1920×1080, Dracula dark theme)

```
┌─────────────────────────────────────────────────────────────────┐
│  Berlin Hbf (bold)                      28.06.2026  14:32       │  ← header (~130 px)
├─────────────────────────────────────────────────────────────────┤
│  14:31   S1   Hauptbahnhof → right: (14:29)                     │  ← departure rows, flex-grow
│  14:35   S2   Flughafen                                        │    delayed: time orange + planned at right
│  …                                                              │
├─────────────────────────────────────────────────────────────────┤
│  🌡 31°    ☀ 4    🌧 20%    (more plugins…)         [⛅ icon]   │  ← plugin bar (163 px)
│  ↓14° ↑28°   ↑9    ↑35%                                         │    groups from bar.d/*.json
└─────────────────────────────────────────────────────────────────┘
```

### Fonts

Text uses DejaVu Sans (regular and bold) loaded via FreeType at runtime from `/usr/share/fonts/truetype/dejavu/`. Twemoji.Mozilla.ttf (COLR v0 color emoji) is loaded as a fallback font chain.

| Variable | Size | Used for |
|----------|------|---------|
| `font_hdr` | 72 px | Header clock, bar main values |
| `font_hdr_bold` | 72 px bold | Station name in header |
| `font_row` | 68 px | Departure rows |
| `font_col` | 36 px | Column headers, sub-labels (min/max, UV max) |
| `font_wea` | 64 px | (legacy; superseded by `font_hdr` in the bar) |

Falls back to `lv_font_montserrat_16` if TTF files cannot be opened.

### Dracula colour palette

| Constant | Hex | Used for |
|---------|-----|---------|
| `C_BG` | `#282a36` | Screen background |
| `C_TEXT` | `#f8f8f2` | Normal text |
| `C_DIM` | `#6272a4` | Planned time, gray arrows (↓ ↑), dim labels |
| `C_DELAY` | `#ffb86c` | Delayed trains |
| `C_CANCEL` | `#ff5555` | Cancelled trains |
| `C_ONTIME` | `#50fa7b` | On-time indicator |
| `C_HEADER` | `#44475a` | Header background |
| `C_ACCENT` | `#bd93f9` | Accent |

### Departure row layout

Each row is an LVGL flex-row container. Children left-to-right:

1. **`time_lbl`** (font_row) — changed time if delayed/early, otherwise planned time
2. **`train_lbl`** (font_row)
3. **`dest_lbl`** (font_row)
4. **spacer** (flex-grow 1)
5. **`planned_lbl`** (font_col, `C_DIM`) — shows `(HH:MM)` when delayed/early, hidden otherwise

### Plugin bar

Fixed 163 px strip at the bottom, built entirely from plugin files in `BAR_DIR` (`/run/bahnhofstafel/bar.d/*.json`, read in filename order — see [`plugins.md`](plugins.md)). Two flex-row boxes: a left box (flex-grow) and a right box hugging the edge; groups with `"align": "right"` go into the right one. Group shapes:

| Group | Structure |
|-------|-----------|
| value group | Outer flex-column: top row [emoji + fixed-width value (font_hdr)], sub-row [0-2 × optional dim arrow + fixed-width value (font_col)] |
| `icon` group | Single large emoji (130 px emoji font) in a 180 px wide cell |

Groups flagged `"dim": true` are drawn at 40 % opacity (placeholder data). Widgets are rebuilt only when the combined layout changes; otherwise only label texts are updated.

The bundled sample plugin is `weather_fetch`: 🌡 temp (↓min ↑max), ☀ UV (↑daily max), 🌧 next-hour rain % (↑daily max), and the condition icon on the right. Before its first fetch it writes a dimmed placeholder.

### WMO weather code → icon vocabulary → symbol mapping

`weather_fetch.c`'s `wmo_to_icon()` maps every code in Open-Meteo's WMO "WW" table (open-meteo.com/en/docs) + `is_day` to a small icon vocabulary string:

| Code(s) | Description (per open-meteo.com/en/docs) | Icon string |
|---|---|---|
| 0 | Clear sky | `clear-day` / `clear-night` |
| 1 | Mainly clear | `clear-day` / `clear-night` |
| 2 | Partly cloudy | `partly-cloudy-day` / `partly-cloudy-night` |
| 3 | Overcast | `cloudy` |
| 45, 48 | Fog and depositing rime fog | `fog` |
| 51, 53, 55 | Drizzle: light, moderate, dense | `drizzle` |
| 56, 57 | Freezing drizzle: light, dense | `sleet` |
| 61, 63, 65 | Rain: slight, moderate, heavy | `rain` |
| 66, 67 | Freezing rain: light, heavy | `sleet` |
| 71, 73, 75 | Snow fall: slight, moderate, heavy | `snow` |
| 77 | Snow grains | `snow-grains` |
| 80, 81, 82 | Rain showers: slight, moderate, violent | `showers` |
| 85, 86 | Snow showers: slight, heavy | `snow-showers` |
| 95 | Thunderstorm: slight or moderate | `thunderstorm` |
| 96, 99 | Thunderstorm with slight/heavy hail (Central Europe only) | `thunderstorm-hail` |
| *(anything else)* | undocumented/unexpected code | `cloudy` (default) |

That icon string is mapped to an emoji by `weather_fetch.c`'s `icon_to_symbol()` and written as the big right-aligned `icon` group of its bar plugin:

| Icon string | Symbol |
|---------|--------|
| `clear-day` | ☀ |
| `clear-night` | 🌙 |
| `partly-cloudy-day` | ⛅ |
| `partly-cloudy-night` | ☁ |
| `cloudy` | ☁ |
| `drizzle` | 🌦 |
| `rain` | 🌧 |
| `showers` | 🌦 |
| `sleet` | 🧊 |
| `snow` | 🌨 |
| `snow-grains` | ❄ |
| `snow-showers` | 🌨 |
| `thunderstorm` | ⛈ |
| `thunderstorm-hail` | ⛈ |
| `fog` | 🌫 |

### File-change detection

`dep_poll_cb` re-renders departures every tick (the minute countdown depends on the clock). `bar_poll_cb` rescans `bar.d/` and only re-reads the plugin files when a name or mtime changed.
