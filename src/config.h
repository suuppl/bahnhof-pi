#pragma once

/* Compile-time DEFAULTS for every setting.
 *
 * All settings marked [runtime] can be changed without rebuilding: put
 * KEY=value in ~/bahnhofstafel/bahnhofstafel.conf (or the environment) and
 * restart — see settings.h and bahnhofstafel.conf.example. The key is the
 * macro name. A `config_local.h` on the include path can still change the
 * compiled-in defaults (every value is #ifndef-guarded). */
#if defined(__has_include)
#  if __has_include("config_local.h")
#    include "config_local.h"
#  endif
#endif

/* ── Station ─────────────────────────────────────────────────────────────
 * [runtime] STATION_NAME is the IRIS station name, looked up at startup
 * (result cached in cache/eva). STATION_EVA_FALLBACK is only used if the
 * lookup fails and there is no cache yet; 0 = none. */
#ifndef STATION_NAME
#define STATION_NAME         "Berlin Hbf"
#endif
#ifndef STATION_EVA_FALLBACK
#define STATION_EVA_FALLBACK 0
#endif

/* [runtime] Only show matching departures. Comma/space list:
 * "S" / "RE" = train type, "S3" / "RE8" = line label, "4012" = train
 * number. "" = show all. */
#ifndef TRAIN_FILTER
#define TRAIN_FILTER ""
#endif

/* [runtime] Minimum delay (minutes, early or late) before a departure is
 * shown as delayed; smaller deviations show as on time. */
#ifndef DELAY_THRESHOLD_MIN
#define DELAY_THRESHOLD_MIN 5
#endif

/* [runtime] Show the platform at the right edge of each row (1/0); a
 * changed platform is shown in orange. */
#ifndef SHOW_PLATFORM
#define SHOW_PLATFORM 0
#endif

/* ── Weather (Open-Meteo, keyless) ─────────────────────────────── [runtime] */
#ifndef OPEN_METEO_URL
#define OPEN_METEO_URL "https://api.open-meteo.com/v1/forecast"
#endif
#ifndef OPEN_METEO_LAT
#define OPEN_METEO_LAT "52.52"
#endif
#ifndef OPEN_METEO_LON
#define OPEN_METEO_LON "13.37"
#endif

/* ── IRIS API base URLs ────────────────────────────────────────── [runtime] */
#ifndef IRIS_STATION_URL
#define IRIS_STATION_URL "https://iris.noncd.db.de/iris-tts/timetable/station"
#endif
#ifndef IRIS_PLAN_URL
#define IRIS_PLAN_URL    "https://iris.noncd.db.de/iris-tts/timetable/plan"
#endif
#ifndef IRIS_RCHG_URL
#define IRIS_RCHG_URL    "https://iris.noncd.db.de/iris-tts/timetable/rchg"
#endif
#ifndef IRIS_FCHG_URL
#define IRIS_FCHG_URL    "https://iris.noncd.db.de/iris-tts/timetable/fchg"
#endif

/* ── Poll intervals ────────────────────────────────────────────── [runtime] */
#ifndef IRIS_INTERVAL_S
#define IRIS_INTERVAL_S    60
#endif
#ifndef WEATHER_INTERVAL_S
#define WEATHER_INTERVAL_S 600
#endif
#ifndef DEP_POLL_MS
#define DEP_POLL_MS        5000   /* display: re-render departures */
#endif
#ifndef BAR_POLL_MS
#define BAR_POLL_MS        1000   /* display: rescan bar.d/ */
#endif

/* ── Install dir, fonts, display device ────────────────────────── [runtime]
 * APP_DIR holds bin/, fonts/, cache/, log/, line_colors.json and
 * bahnhofstafel.conf. Paths below it (CACHE_DIR, EVA_CACHE, WEATHER_CACHE,
 * EMOJI_FONT_PATH, LINE_COLORS_JSON) are derived at runtime, see settings.h. */
#ifndef APP_DIR
#define APP_DIR "/home/dietpi/bahnhofstafel"
#endif
#ifndef FONT_PATH
#define FONT_PATH      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
#endif
#ifndef FONT_BOLD_PATH
#define FONT_BOLD_PATH "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
#endif
#ifndef FB_DEVICE
#define FB_DEVICE      "/dev/fb0"
#endif

/* ── Screen brightness (brightness_ctl plugin, DDC/CI over HDMI I2C) [runtime]
 * Raw writes, no readback (ddcutil verify-reads are unreliable on the Pi).
 * Smooth day/night curve around sunrise/sunset from weather.json. */
#ifndef BRIGHT_DAY_PCT
#define BRIGHT_DAY_PCT         100   /* daytime brightness (%) */
#endif
#ifndef BRIGHT_NIGHT_PCT
#define BRIGHT_NIGHT_PCT       0     /* nighttime brightness (%) */
#endif
#ifndef BRIGHT_RAMP_S
#define BRIGHT_RAMP_S          2700  /* half-width of the dawn/dusk transition */
#endif
#ifndef BRIGHT_INTERVAL_S
#define BRIGHT_INTERVAL_S      30    /* recompute/write interval */
#endif
#ifndef BRIGHT_FALLBACK_RISE_H
#define BRIGHT_FALLBACK_RISE_H 7     /* used when weather.json is unavailable */
#endif
#ifndef BRIGHT_FALLBACK_SET_H
#define BRIGHT_FALLBACK_SET_H  21
#endif
#ifndef I2C_BUS_PATH
#define I2C_BUS_PATH           "/dev/i2c-2"
#endif
#ifndef DDC_I2C_ADDR
#define DDC_I2C_ADDR           0x37
#endif
#ifndef VCP_BRIGHTNESS
#define VCP_BRIGHTNESS         0x10  /* MCCS brightness feature */
#endif

/* ── Presence sensor (presence_ctl plugin, DFRobot C4001, UART) ─ [runtime] */
#ifndef PRESENCE_DEVICE
#define PRESENCE_DEVICE    "/dev/ttyAMA0"
#endif
#ifndef PRESENCE_TIMEOUT_S
#define PRESENCE_TIMEOUT_S 300   /* screen off after 5 min without presence */
#endif

/* ── Fixed at build time ──────────────────────────────────────────────────
 * IPC paths are shared by every program and by the systemd/tmpfiles setup;
 * array sizes are compile-time. */
#define DATA_DIR         "/run/bahnhofstafel"
#define DEPARTURES_JSON  DATA_DIR "/departures.json"
#define WEATHER_JSON     DATA_DIR "/weather.json"
/* Bottom-bar plugins: every *.json in here contributes groups, in filename
 * order (see docs/plugins.md). weather_fetch writes 10-weather.json. */
#define BAR_DIR          DATA_DIR "/bar.d"
#define BAR_WEATHER_JSON BAR_DIR "/10-weather.json"
/* Screen on/off requests from plugins (screen.h): screen is on unless some
 * file in here says {"on": false}. presence_ctl writes 50-presence.json. */
#define SCREEN_DIR       DATA_DIR "/screen.d"

#define MAX_DEPARTURES  120
#define MAX_ROWS        16   /* max departure rows the display can show */
