# Bottom-bar plugins, site config, and embedding

The bottom strip of the display is owned entirely by **plugins**: small
programs that write a JSON file into `/run/bahnhofstafel/bar.d/`. The
display knows nothing about weather, solar, or anything else. It just draws
whatever groups the files contain.

`weather_fetch` is the bundled sample plugin.

## Plugin files — `bar.d/NN-name.json`

- Every `*.json` in `BAR_DIR` (`/run/bahnhofstafel/bar.d`) is read; files are
  concatenated in **filename order**, hence the `NN-` prefix (weather uses
  `10-weather.json`).
- Write atomically (`write_atomic()` from `src/fileio.c`: temp file +
  `rename()`), or the display may read a half-written file.
- Delete your file to remove your groups. The display rescans every
  `BAR_POLL_MS` and only rebuilds widgets when the layout changes.
- Limits: 16 files, 16 groups total, 2 sub-values per group.

```json
{
  "groups": [
    { "emoji": "🌡", "value": "21°", "width": 170,
      "sub": [ { "arrow": "↓", "value": "14°", "width": 90 },
               { "arrow": "↑", "value": "25°", "width": 90 } ] },
    { "icon": "⛅", "align": "right" }
  ]
}
```

| Field   | Meaning |
|---------|---------|
| `emoji` | Glyph left of the top value |
| `value` | Pre-formatted text (the plugin decides units/formatting) |
| `width` | Fixed label width in px so the bar doesn't reflow as values change (default 240 top / 160 sub) |
| `sub`   | 0-2 smaller values below the top value, each `{ arrow?, value, width? }` |
| `arrow` | Optional dim glyph before a sub-value |
| `icon`  | Makes this a big single-emoji group (no `emoji`/`value`/`sub`) |
| `align` | `"left"` (default) or `"right"`; right groups hug the right edge |
| `dim`   | `true` = draw at 40 % opacity, e.g. placeholder before the first real data |

### The sample: `weather_fetch`

`src/weather_fetch.c` polls Open-Meteo, writes `weather.json` (consumed by
`brightness_ctl` for sunrise/sunset), and renders its plugin file in
`write_bar()`:

- 🌡 temperature, ↓ min / ↑ max
- ☀ UV index, ↑ daily max
- 🌧 next-hour rain %, ↑ daily max
- the condition emoji as a right-aligned `icon` group

Before the first fetch (no cache) it writes a dimmed `–` placeholder so the
layout is in place immediately. `weather_fetch --demo` cycles worst-case
values to check label widths.

### Writing your own

Any language works; a C plugin can reuse the shared helpers:

```c
#include "config.h"   /* BAR_DIR */
#include "schema.h"   /* BAR_* field names */
#include "fileio.h"   /* write_atomic() */
#include "http.h"     /* http_get() */
...
mkdir(BAR_DIR, 0755);
write_atomic(BAR_DIR "/50-myplugin.json", json);
```

A shell one-off works too:

```sh
echo '{"groups":[{"emoji":"🌡","value":"19°","width":170}]}' > /run/bahnhofstafel/bar.d/.tmp \
  && mv /run/bahnhofstafel/bar.d/.tmp /run/bahnhofstafel/bar.d/60-room.json
```

## Site settings — `bahnhofstafel.conf`

Everything site-specific is a **runtime setting**: `KEY=value` lines in
`~/bahnhofstafel/bahnhofstafel.conf` on the Pi, read at startup (restart to
apply). Keys are the macro names from `src/config.h`, which holds the
compiled-in defaults; environment variables override the file. All keys are
listed in [`bahnhofstafel.conf.example`](../bahnhofstafel.conf.example).

```sh
STATION_NAME="Hamburg Hbf"     # IRIS station name, looked up at startup
OPEN_METEO_LAT=53.55
OPEN_METEO_LON=10.01
TRAIN_FILTER="S, RE8"          # optional: type, line label or train number
DELAY_THRESHOLD_MIN=5
```

Keep your copy as `bahnhofstafel.conf` next to the `Makefile` (git-ignored);
`make deploy` installs it, `make config` pushes it and restarts. Plugins can
use the same mechanism (`cfg_str()` / `cfg_long()` from `src/settings.h`,
linked via `bahnhof_common`) for their own keys.

Train line colours live in `line_colors.json` (see [`line-colors.md`](line-colors.md)).

Deploy settings (`PI`, `SSH_KEY`, `APP_DIR`, `EXTRA_BINS`, `CONFIG_FILE`,
`LINE_COLORS_FILE`) go in a git-ignored `local.mk` next to the `Makefile`, or
on the `make` command line.

A `config_local.h` on the include path (or `-DBAHNHOF_LOCAL_CONFIG_DIR=<dir>`)
can still change the compiled-in defaults, but is rarely needed now.

## Embedding in a private repo

Keep site data and private plugins in your own repo and include this one as
a submodule:

```
my-site/
├── public/                 # this repo (git submodule)
├── bahnhofstafel.conf      # site settings (runtime)
├── src/my_plugin.c         # writes bar.d/50-my.json
├── CMakeLists.txt
└── Makefile
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_site C)
add_subdirectory(public)
add_executable(my_plugin src/my_plugin.c)
target_link_libraries(my_plugin PRIVATE bahnhof_common)   # http.c, fileio.c, curl, cJSON
```

`Makefile`:

```make
PUB        := public
SRC_DIR    := .
EXTRA_BINS := my_plugin
PI         := pi@192.0.2.10
include public/Makefile
```

`make deploy` copies `EXTRA_BINS` to the Pi and writes their names to
`~/bahnhofstafel/extra_daemons`; `start.sh` starts each one with its own log
file.
