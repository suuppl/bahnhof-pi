/*
 * weather_fetch — periodically polls the Open-Meteo forecast API (free,
 * keyless JSON) and writes /run/bahnhofstafel/weather.json atomically.
 *
 * It is also the sample bottom-bar plugin (docs/plugins.md): every update is
 * rendered into BAR_WEATHER_JSON as 🌡 temp (↓min ↑max), ☀ UV (↑max),
 * 🌧 next-hour rain % (↑daily max) and a big right-aligned condition icon.
 *
 * Single request per cycle, with timezone=auto so every timestamp in the
 * response (sunrise/sunset) is already the station's local wall-clock time
 * as a plain "YYYY-MM-DDTHH:MM" string — no UTC-offset math needed, just
 * sscanf()+mktime() (the Pi's system TZ matches this station's one fixed
 * location; see IRIS time parsing gotcha).
 *
 * Current conditions (from "current"):
 *   temp, uvi, weather_code, is_day
 * Daily range (from "daily", index 0 = today):
 *   tempmin, tempmax, max_uvi, precipprob_max, sunrise, sunset
 * Next hour (from "hourly", first entry whose timestamp is >= now):
 *   precipprob
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include "config.h"
#include "http.h"
#include "fileio.h"
#include "schema.h"
#include "weather_info.h"

/* ── bottom-bar plugin output ───────────────────────────────────────────── */

/* Icon vocabulary (wmo_to_icon below) → emoji glyph. */
static const char *icon_to_symbol(const char *icon) {
    if (!icon || !icon[0]) return "";

    if (strcmp(icon, "clear-day")           == 0) return "☀";
    if (strcmp(icon, "clear-night")         == 0) return "🌙";
    if (strcmp(icon, "cloudy")              == 0) return "☁";
    if (strcmp(icon, "partly-cloudy-day")   == 0) return "⛅";
    if (strcmp(icon, "partly-cloudy-night") == 0) return "☁";
    if (strcmp(icon, "drizzle")             == 0) return "🌦";
    if (strcmp(icon, "rain")                == 0) return "🌧";
    if (strcmp(icon, "showers")             == 0) return "🌦";
    if (strcmp(icon, "sleet")               == 0) return "🧊";
    if (strcmp(icon, "snow")                == 0) return "🌨";
    if (strcmp(icon, "snow-grains")         == 0) return "❄";
    if (strcmp(icon, "snow-showers")        == 0) return "🌨";
    if (strcmp(icon, "thunderstorm")        == 0) return "⛈";
    if (strcmp(icon, "thunderstorm-hail")   == 0) return "⛈";
    if (strcmp(icon, "fog")                 == 0) return "🌫";

    return "";
}

static cJSON *bar_group(cJSON *groups, const char *emoji, const char *value, int width, int dim) {
    cJSON *g = cJSON_CreateObject();
    cJSON_AddStringToObject(g, BAR_EMOJI, emoji);
    cJSON_AddStringToObject(g, BAR_VALUE, value);
    cJSON_AddNumberToObject(g, BAR_WIDTH, width);
    if (dim) cJSON_AddTrueToObject(g, BAR_DIM);
    cJSON_AddItemToArray(groups, g);
    return cJSON_AddArrayToObject(g, BAR_SUB);
}

static void bar_sub(cJSON *subs, const char *arrow, const char *value, int width) {
    cJSON *sl = cJSON_CreateObject();
    if (arrow) cJSON_AddStringToObject(sl, BAR_ARROW, arrow);
    cJSON_AddStringToObject(sl, BAR_VALUE, value);
    cJSON_AddNumberToObject(sl, BAR_WIDTH, width);
    cJSON_AddItemToArray(subs, sl);
}

/* Format "%.0f<suffix>", or "–" when the value is missing (NAN). */
static const char *fmt0(char *buf, size_t sz, double v, const char *suffix) {
    if (v != v) snprintf(buf, sz, "\xe2\x80\x93");
    else        snprintf(buf, sz, "%.0f%s", v, suffix);
    return buf;
}

/* Write the weather as bar groups. w == NULL writes a dimmed placeholder so
 * the layout is in place before the first fetch. Widths cover the worst case
 * ("-19°"/"-25°"/"-11°", UV "11"/"12", "100%"). */
static int write_bar(const WeatherInfo *w) {
    static const char *DOWN = "\xe2\x86\x93", *UP = "\xe2\x86\x91";
    WeatherInfo none = { NAN, NAN, NAN, NAN, NAN, NAN, NAN, 0, 0, "" };
    int dim = (w == NULL);
    if (!w) w = &none;
    char b[32];

    cJSON *root = cJSON_CreateObject();
    cJSON *groups = cJSON_AddArrayToObject(root, BAR_GROUPS);

    cJSON *sub = bar_group(groups, "\xf0\x9f\x8c\xa1", fmt0(b, sizeof(b), w->temp, "\xc2\xb0"), 170, dim); /* 🌡 */
    bar_sub(sub, DOWN, fmt0(b, sizeof(b), w->tempmin, "\xc2\xb0"), 90);
    bar_sub(sub, UP,   fmt0(b, sizeof(b), w->tempmax, "\xc2\xb0"), 90);

    sub = bar_group(groups, "\xe2\x98\x80", fmt0(b, sizeof(b), w->uvi, ""), 100, dim);         /* ☀ */
    bar_sub(sub, UP, fmt0(b, sizeof(b), w->max_uvi, ""), 60);

    sub = bar_group(groups, "\xf0\x9f\x8c\xa7", fmt0(b, sizeof(b), w->precipprob, "%"), 210, dim); /* 🌧 */
    bar_sub(sub, UP, fmt0(b, sizeof(b), w->precipprob_max, "%"), 120);

    cJSON *icon = cJSON_CreateObject();
    cJSON_AddStringToObject(icon, BAR_BIG_ICON, dim ? "" : icon_to_symbol(w->icon));
    cJSON_AddStringToObject(icon, BAR_ALIGN, "right");
    cJSON_AddItemToArray(groups, icon);

    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    mkdir(BAR_DIR, 0755);
    int rc = (str && write_atomic(BAR_WEATHER_JSON, str) == 0) ? 0 : -1;
    if (rc) fprintf(stderr, "weather_fetch: failed to write %s\n", BAR_WEATHER_JSON);
    free(str);
    return rc;
}

/* WMO weather code (per open-meteo.com/en/docs "WMO Weather interpretation
 * codes (WW)" table) -> the small icon vocabulary icon_to_symbol() maps.
 * Every documented code is listed explicitly; anything undocumented falls
 * back to "cloudy". */
static const char *wmo_to_icon(int code, int is_day) {
    switch (code) {
    case 0:                     return is_day ? "clear-day" : "clear-night";        /* Clear sky */
    case 1:                     return is_day ? "clear-day" : "clear-night";        /* Mainly clear */
    case 2:                     return is_day ? "partly-cloudy-day" : "partly-cloudy-night"; /* Partly cloudy */
    case 3:                     return "cloudy";                                   /* Overcast */
    case 45: case 48:           return "fog";                                      /* Fog / depositing rime fog */
    case 51: case 53: case 55:  return "drizzle";                                  /* Drizzle: light/moderate/dense */
    case 56: case 57:           return "sleet";                                    /* Freezing drizzle: light/dense */
    case 61: case 63: case 65:  return "rain";                                     /* Rain: slight/moderate/heavy */
    case 66: case 67:           return "sleet";                                    /* Freezing rain: light/heavy */
    case 71: case 73: case 75:  return "snow";                                     /* Snow fall: slight/moderate/heavy */
    case 77:                    return "snow-grains";                              /* Snow grains */
    case 80: case 81: case 82:  return "showers";                                  /* Rain showers: slight/moderate/violent */
    case 85: case 86:           return "snow-showers";                             /* Snow showers: slight/heavy */
    case 95:                    return "thunderstorm";                             /* Thunderstorm: slight or moderate */
    case 96: case 99:           return "thunderstorm-hail";                        /* Thunderstorm with slight/heavy hail */
    default:                    return "cloudy";
    }
}

/* Parses an Open-Meteo local ISO8601 timestamp ("YYYY-MM-DDTHH:MM") into
 * genuine epoch seconds via mktime() — safe since timezone=auto already
 * returns station-local time and the Pi's system TZ matches it. */
static time_t parse_iso_local(const char *iso) {
    if (!iso) return 0;
    int y, mo, d, h, mi;
    if (sscanf(iso, "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) < 5) return 0;
    struct tm t = {0};
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = h; t.tm_min = mi; t.tm_isdst = -1;
    return mktime(&t);
}

/* First value in an hourly `field` array whose parallel `time` entry is
 * >= now — i.e. the next full hour's forecast (skips the already-elapsed
 * current hour, which is always the array's first, oldest entry). */
static double next_hour_value(cJSON *hourly, const char *field) {
    cJSON *htime = cJSON_GetObjectItem(hourly, "time");
    cJSON *hval  = cJSON_GetObjectItem(hourly, field);
    if (!cJSON_IsArray(htime) || !cJSON_IsArray(hval)) return 0;
    time_t now = time(NULL);
    int n = cJSON_GetArraySize(htime);
    for (int i = 0; i < n; i++) {
        const char *iso = cJSON_GetStringValue(cJSON_GetArrayItem(htime, i));
        if (parse_iso_local(iso) < now) continue;
        cJSON *v = cJSON_GetArrayItem(hval, i);
        return cJSON_IsNumber(v) ? v->valuedouble : 0;
    }
    return 0;
}

/* ── parse Open-Meteo response and write weather.json ───────────────────── */

static int process(const char *raw) {
    cJSON *root = cJSON_Parse(raw);
    if (!root) {
        fprintf(stderr, "weather_fetch: JSON parse error\n");
        return -1;
    }

    cJSON *cur = cJSON_GetObjectItem(root, "current");
    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    cJSON *hourly = cJSON_GetObjectItem(root, "hourly");
    if (!cur || !daily || !hourly) {
        fprintf(stderr, "weather_fetch: missing current/daily/hourly\n");
        cJSON_Delete(root);
        return -1;
    }

    double temp = cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "temperature_2m"));
    double uvi  = cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "uv_index"));
    int weather_code = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "weather_code"));
    int is_day = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "is_day"));

    double tempmin = cJSON_GetNumberValue(cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "temperature_2m_min"), 0));
    double tempmax = cJSON_GetNumberValue(cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "temperature_2m_max"), 0));
    double max_uvi = cJSON_GetNumberValue(cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "uv_index_max"), 0));
    double precipprob_max = cJSON_GetNumberValue(cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "precipitation_probability_max"), 0));
    double precipprob = next_hour_value(hourly, "precipitation_probability");

    const char *sunrise_iso = cJSON_GetStringValue(cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "sunrise"), 0));
    const char *sunset_iso  = cJSON_GetStringValue(cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "sunset"), 0));
    time_t sunrise = parse_iso_local(sunrise_iso);
    time_t sunset  = parse_iso_local(sunset_iso);

    const char *icon = wmo_to_icon(weather_code, is_day);

    WeatherInfo info = {0};
    info.temp = temp;
    info.tempmin = tempmin;
    info.tempmax = tempmax;
    info.uvi = uvi;
    info.max_uvi = max_uvi;
    info.precipprob = precipprob;
    info.precipprob_max = precipprob_max;
    info.sunrise = (double)sunrise;
    info.sunset = (double)sunset;
    snprintf(info.icon, sizeof(info.icon), "%s", icon);

    cJSON *out = serialize_weather_info(&info);
    cJSON_AddNumberToObject(out, "fetched_at", (double)time(NULL));

    cJSON_Delete(root);

    char *json_str = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    if (!json_str) return -1;

    int ok = write_atomic(WEATHER_JSON, json_str) == 0;
    free(json_str);
    if (!ok) return -1;
    write_bar(&info);

    /* persist to cache so data is available immediately after reboot */
    copy_atomic(WEATHER_JSON, WEATHER_CACHE);
    return 0;
}

/* ── demo mode: cycle canned values to exercise the weather bar UI ──────── */

#define DEMO_INTERVAL_S 2

typedef struct {
    double temp, tempmin, tempmax, uvi, max_uvi, precipprob, precipprob_max;
    const char *icon;
} DemoCase;

/* One case per icon string plus a few boundary values (zeros, blank icon,
 * double-digit negative temp, 100% precip) to exercise label widths and
 * the "unknown icon -> blank glyph" fallback path.
 *
 * The first entry is the maximum-width case: every temp field is a
 * double-digit negative (widest "-NN°" label in all three temp slots at
 * once), uvi == max_uvi and precipprob == precipprob_max (no headroom
 * between the main and sub value in either pair), 100% precip, "rain"
 * icon. */
static const DemoCase demo_cases[] = {
    { -19, -25, -11,   4,   4, 100, 100, "rain" },
    {  32,  18,  34,   9,  10,   0,   0, "clear-day" },
    {  -5,  -8,   2,   0,   1,   0,   0, "clear-night" },
    {  18,  10,  20,   4,   5,  20,  35, "partly-cloudy-day" },
    {   8,   4,  10,   0,   1,  10,  20, "partly-cloudy-night" },
    {  12,   8,  14,   1,   2,  40,  55, "cloudy" },
    {   5,   2,   9,   0,   1,  10,  15, "fog" },
    {  14,  11,  16,   2,   3,  60,  70, "drizzle" },
    {  13,  10,  15,   1,   2,  85,  90, "rain" },
    {  19,  14,  22,   5,   6,  55,  75, "showers" },
    {   0,  -2,   2,   0,   1,  70,  80, "sleet" },
    {  -3,  -6,  -1,   0,   1,  90,  95, "snow" },
    {  -1,  -4,   1,   0,   1,  30,  40, "snow-grains" },
    {  -2,  -5,   0,   0,   1,  65,  80, "snow-showers" },
    {  24,  19,  27,   3,   8,  95, 100, "thunderstorm" },
    {  22,  17,  25,   2,   7, 100, 100, "thunderstorm-hail" },
    {  35,  25,  38,  11,  12,   0,   5, "clear-day" },
    {   0,   0,   0,   0,   0,   0,   0, "" },
    { -15, -20, -10,   0,   0,  15,  25, "clear-night" },
};
#define DEMO_CASE_COUNT ((int)(sizeof(demo_cases) / sizeof(demo_cases[0])))

static int write_demo(const DemoCase *c) {
    time_t now = time(NULL);

    WeatherInfo info = {0};
    info.temp = c->temp;
    info.tempmin = c->tempmin;
    info.tempmax = c->tempmax;
    info.uvi = c->uvi;
    info.max_uvi = c->max_uvi;
    info.precipprob = c->precipprob;
    info.precipprob_max = c->precipprob_max;
    /* synthetic, not meant to match the real path's computation (see ADR 0003) */
    info.sunrise = (double)(now - 6 * 3600);
    info.sunset  = (double)(now + 6 * 3600);
    snprintf(info.icon, sizeof(info.icon), "%s", c->icon);

    cJSON *out = serialize_weather_info(&info);
    cJSON_AddNumberToObject(out, "fetched_at", (double)now);

    char *json_str = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    if (!json_str) return -1;

    int ok = write_atomic(WEATHER_JSON, json_str) == 0;
    free(json_str);
    write_bar(&info);
    return ok ? 0 : -1;
}

static void run_demo(void) {
    mkdir(DATA_DIR, 0755);
    printf("weather_fetch: demo mode — cycling %d cases every %ds (Ctrl+C to stop)\n",
           DEMO_CASE_COUNT, DEMO_INTERVAL_S);
    for (int i = 0; ; i = (i + 1) % DEMO_CASE_COUNT) {
        const DemoCase *c = &demo_cases[i];
        if (write_demo(c) == 0)
            printf("weather_fetch: demo [%d/%d] icon=%-18s temp=%.0f (%.0f/%.0f) uvi=%.0f/%.0f precipprob=%.0f/%.0f%%\n",
                   i + 1, DEMO_CASE_COUNT, c->icon[0] ? c->icon : "(none)",
                   c->temp, c->tempmin, c->tempmax, c->uvi, c->max_uvi,
                   c->precipprob, c->precipprob_max);
        sleep(DEMO_INTERVAL_S);
    }
}

/* ── main ───────────────────────────────────────────────────────────────── */

static void restore_cache(void) {
    struct stat s;
    if (stat(WEATHER_JSON, &s) != 0 && copy_atomic(WEATHER_CACHE, WEATHER_JSON) == 0)
        printf("weather_fetch: restored cached weather data\n");

    /* render the bar from whatever weather.json we have, else a placeholder */
    char *raw = read_file(WEATHER_JSON);
    cJSON *root = raw ? cJSON_Parse(raw) : NULL;
    free(raw);
    if (root) {
        WeatherInfo info = parse_weather_info(root);
        write_bar(&info);
        cJSON_Delete(root);
    } else {
        write_bar(NULL);
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--demo") == 0) {
        run_demo();
        return 0;
    }

    mkdir(DATA_DIR, 0755);
    mkdir(CACHE_DIR, 0755);
    restore_cache();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    for (;;) {
        char url[600];
        snprintf(url, sizeof(url),
            "%s?latitude=%s&longitude=%s"
            "&daily=sunrise,sunset,uv_index_max,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
            "&hourly=precipitation_probability"
            "&current=temperature_2m,weather_code,is_day,uv_index"
            "&timezone=auto&forecast_days=2", /* 2 days so "next hour" is available near midnight */
            OPEN_METEO_URL, OPEN_METEO_LAT, OPEN_METEO_LON);

        char *raw = http_get(url);
        if (raw) {
            if (process(raw) == 0)
                printf("weather_fetch: updated %s\n", WEATHER_JSON);
            free(raw);
        }
        sleep(WEATHER_INTERVAL_S);
    }

    curl_global_cleanup();
    return 0;
}
