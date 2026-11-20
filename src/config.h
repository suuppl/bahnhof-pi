#pragma once

/* Site-specific overrides. Put your own station / coordinates / paths in a
 * `config_local.h` anywhere on the include path (e.g. src/config_local.h,
 * git-ignored, or a directory passed via -DBAHNHOF_LOCAL_CONFIG_DIR=...).
 * Every setting below is wrapped in #ifndef so the local file wins. */
#if defined(__has_include)
#  if __has_include("config_local.h")
#    include "config_local.h"
#  endif
#endif

/* Station — example default; override in config_local.h. STATION_NAME is
 * the IRIS station name used for the live EVA lookup at startup (result is
 * cached in EVA_CACHE). STATION_EVA_FALLBACK is only used if the lookup
 * fails and there is no cache yet; 0 = none. */
#ifndef STATION_NAME
#define STATION_NAME     "Berlin Hbf"
#endif
#ifndef STATION_EVA_FALLBACK
#define STATION_EVA_FALLBACK 0
#endif

/* Train line pill colours: { prefix, background, text }, first match wins
 * (put longer prefixes like "S20" before "S2"). Unmatched lines are grey. */
#ifndef LINE_COLORS
#define LINE_COLORS { \
    { "S",   0x408335, 0xffffff }, /* S-Bahn green */ \
    { "RE",  0xec0016, 0xffffff }, /* regional red */ \
    { "RB",  0xec0016, 0xffffff }, \
    { "ICE", 0xf8f8f2, 0x282a36 }, /* long-distance white */ \
    { "IC",  0xf8f8f2, 0x282a36 }, \
    { "EC",  0xf8f8f2, 0x282a36 }, \
}
#endif

/* Weather (Open-Meteo — free JSON API, no API key required) */
#define OPEN_METEO_URL "https://api.open-meteo.com/v1/forecast"
#ifndef OPEN_METEO_LAT
#define OPEN_METEO_LAT "52.52"
#endif
#ifndef OPEN_METEO_LON
#define OPEN_METEO_LON "13.37"
#endif

/* Install directory on the Pi (binaries, fonts, cache, logs live below it) */
#ifndef APP_DIR
#define APP_DIR "/home/dietpi/bahnhofstafel"
#endif

/* IRIS API base URLs */
#define IRIS_STATION_URL "https://iris.noncd.db.de/iris-tts/timetable/station"
#define IRIS_PLAN_URL    "https://iris.noncd.db.de/iris-tts/timetable/plan"
#define IRIS_RCHG_URL    "https://iris.noncd.db.de/iris-tts/timetable/rchg"
#define IRIS_FCHG_URL    "https://iris.noncd.db.de/iris-tts/timetable/fchg"

/* IPC paths */
#define DATA_DIR         "/run/bahnhofstafel"
#define DEPARTURES_JSON  DATA_DIR "/departures.json"
#define WEATHER_JSON     DATA_DIR "/weather.json"
/* Bottom-bar plugins: every *.json in here contributes groups, in filename
 * order (see docs/plugins.md). weather_fetch writes 10-weather.json. */
#define BAR_DIR          DATA_DIR "/bar.d"
#define BAR_WEATHER_JSON BAR_DIR "/10-weather.json"
#define PRESENCE_JSON    DATA_DIR "/presence.json"
/* Persistent weather cache — survives reboots so display shows data immediately */
#define CACHE_DIR        APP_DIR "/cache"
#define WEATHER_CACHE    CACHE_DIR "/weather.json"
#define EVA_CACHE        CACHE_DIR "/eva"
#define EMOJI_FONT_PATH  APP_DIR "/fonts/Twemoji.Mozilla.ttf"

/* Poll intervals */
#define IRIS_INTERVAL_S    60
#define WEATHER_INTERVAL_S 600

/* Display update intervals (ms, used by LVGL timers) */
#define DEP_POLL_MS     5000
#define BAR_POLL_MS     1000

/* Max departures stored */
#define MAX_DEPARTURES  120
#define MAX_ROWS        16   /* max departure rows the display can show */

/* Screen brightness — real monitor backlight via DDC/CI over the HDMI I2C bus
 * (raw write, no readback: ddcutil verify-reads are unreliable on this Pi).
 * Derived from sunrise/sunset now; a room-light sensor can later feed the same
 * 0..100 percent. Handled by the brightness_ctl daemon. */
#define BRIGHT_DAY_PCT    100   /* daytime brightness (%) */
#define BRIGHT_NIGHT_PCT  0     /* nighttime brightness (%) */
#define BRIGHT_RAMP_S     2700  /* half-width of the dawn/dusk transition (45 min) */
#define BRIGHT_INTERVAL_S 30    /* how often brightness_ctl recomputes/writes */
#define BRIGHT_FALLBACK_RISE_H  7   /* fallback sunrise hour when weather.json unavailable */
#define BRIGHT_FALLBACK_SET_H   21  /* fallback sunset hour when weather.json unavailable */

/* DDC/CI over I2C (HDMI DDC bus). Brightness = MCCS VCP feature 0x10. */
#ifndef I2C_BUS_PATH
#define I2C_BUS_PATH      "/dev/i2c-2"
#endif
#define DDC_I2C_ADDR      0x37
#define VCP_BRIGHTNESS    0x10

/* Presence sensor (DFRobot C4001 mmWave, UART) */
#ifndef PRESENCE_DEVICE
#define PRESENCE_DEVICE      "/dev/ttyAMA0"
#endif
#define PRESENCE_TIMEOUT_S   300   /* screen off after 5 min without presence */
