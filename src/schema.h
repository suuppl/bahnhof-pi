#pragma once

/* JSON field name constants for the IPC files under /run/bahnhofstafel/.
 * Used by both producers (iris_fetch, weather_fetch) and consumers
 * (display, brightness_ctl, plugins) so renames propagate at compile time. */

/* departures.json — produced by iris_fetch, consumed by display */
#define DEP_PLANNED_TIME  "planned_time"
#define DEP_CHANGED_TIME  "changed_time"
#define DEP_PLANNED_TS    "planned_ts"   /* unix epoch seconds */
#define DEP_CHANGED_TS    "changed_ts"   /* unix epoch seconds, 0 if none */
#define DEP_TRAIN         "train"
#define DEP_DESTINATION   "destination"
#define DEP_STATUS        "status"
#define DEP_DELAY_MIN     "delay_min"
#define DEP_PLANNED_PLATFORM "planned_platform"
#define DEP_CHANGED_PLATFORM "changed_platform"
#define DEP_MARK          "mark"         /* direction emoji (DIR_MARKS), "" if none */
#define DEP_MARK_BG       "mark_bg"      /* its circle colour "#rrggbb", "" = none */
/* departures.json root */
#define DEP_SINGLE_LINE   "single_line"  /* the only line in the fetched window, "" if several */
#define DEP_MAIN_LINE     "main_line"    /* the most frequent line in the fetched window */

/* bar.d/NN-name.json — bottom-bar plugin files, consumed by display (docs/plugins.md)
 * { "groups": [ { "emoji": "🌡", "value": "21°", "width": 170,
 *                 "sub": [ { "arrow": "↓", "value": "14°", "width": 90 } ] },
 *               { "icon": "⛅", "align": "right" } ] } */
/* screen.d/NN-name.json — screen on/off requests (screen.h) */
#define SCR_ON            "on"      /* false = this plugin wants the screen off */

#define BAR_GROUPS        "groups"
#define BAR_EMOJI         "emoji"   /* small glyph left of the value */
#define BAR_VALUE         "value"   /* pre-formatted text */
#define BAR_WIDTH         "width"   /* fixed label width in px */
#define BAR_SUB           "sub"     /* 0-2 smaller values below */
#define BAR_ARROW         "arrow"   /* optional dim glyph before a sub value */
#define BAR_BIG_ICON      "icon"    /* large emoji instead of emoji+value */
#define BAR_ALIGN         "align"   /* "left" (default) or "right" */
#define BAR_DIM           "dim"     /* true = render at 40% (placeholder data) */

/* weather.json — produced by weather_fetch, consumed by display + brightness_ctl */
#define WEA_TEMP          "temp"
#define WEA_TEMPMIN       "tempmin"
#define WEA_TEMPMAX       "tempmax"
#define WEA_UVI           "uvi"
#define WEA_MAX_UVI       "max_uvi"
#define WEA_PRECIPPROB    "precipprob"
#define WEA_PRECIPPROB_MAX "precipprob_max"
#define WEA_SUNRISE       "sunrise"
#define WEA_SUNSET        "sunset"
#define WEA_ICON          "icon"
