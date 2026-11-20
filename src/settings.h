#pragma once
/* Runtime settings (restart to apply).
 *
 * Every tunable in config.h can be overridden at runtime, by key = macro
 * name, from (highest priority first):
 *   1. the process environment          (e.g. STATION_NAME="Köln Hbf")
 *   2. the settings file                $BAHNHOF_CONF, else
 *                                       $APP_DIR/bahnhofstafel.conf
 *   3. the compile-time default         config.h / config_local.h
 *
 * The file is plain KEY=value lines (shell-compatible, '#' comments,
 * optional quotes and "export "). See bahnhofstafel.conf.example.
 *
 * Usage: CFG_S(STATION_NAME), CFG_I(BRIGHT_DAY_PCT) — the key is the macro's
 * name, the default its config.h value. */

const char *cfg_str(const char *key, const char *dflt);
long        cfg_long(const char *key, long dflt);

/* APP_DIR (runtime) + "/" + rel, allocated once per call site's string. */
const char *cfg_app_path(const char *rel);

#define CFG_S(name) cfg_str(#name, name)
#define CFG_I(name) cfg_long(#name, name)

/* Paths below APP_DIR, each individually overridable too */
#define CACHE_DIR        cfg_str("CACHE_DIR",        cfg_app_path("cache"))
#define WEATHER_CACHE    cfg_str("WEATHER_CACHE",    cfg_app_path("cache/weather.json"))
#define EVA_CACHE        cfg_str("EVA_CACHE",        cfg_app_path("cache/eva"))
#define EMOJI_FONT_PATH  cfg_str("EMOJI_FONT_PATH",  cfg_app_path("fonts/Twemoji.Mozilla.ttf"))
#define LINE_COLORS_JSON cfg_str("LINE_COLORS_JSON", cfg_app_path("line_colors.json"))
