#pragma once

#include <cjson/cJSON.h>

/* The complete value that crosses the weather.json IPC boundary — the
 * current weather reading, not including envelope metadata like
 * "fetched_at" (staleness/provenance, not part of the reading itself,
 * handled separately by weather_fetch.c's top-level write code).
 *
 * Numeric fields absent from the source JSON parse to NAN (see
 * departure_info.h for the rationale) — check with isnan(). This matters
 * here more than for departures: 0.0 is a legitimate reading for several
 * of these fields (0°C, 0% precip probability), so it can't double as an
 * "absent" sentinel. */
typedef struct {
    double temp;             /* WEA_TEMP, current temperature C */
    double tempmin;          /* WEA_TEMPMIN, today's forecast low */
    double tempmax;          /* WEA_TEMPMAX, today's forecast high */
    double uvi;               /* WEA_UVI, current UV index */
    double max_uvi;           /* WEA_MAX_UVI, today's forecast max UV index */
    double precipprob;        /* WEA_PRECIPPROB, next-hour precip probability % */
    double precipprob_max;    /* WEA_PRECIPPROB_MAX, today's forecast max precip probability % */
    double sunrise;           /* WEA_SUNRISE, unix epoch seconds */
    double sunset;            /* WEA_SUNSET, unix epoch seconds */
    char   icon[32];          /* WEA_ICON, e.g. "clear-day"/"rain"/"" */
} WeatherInfo;

/* Parse the weather.json root object into a typed value. */
WeatherInfo parse_weather_info(const cJSON *root);

/* Serialize a WeatherInfo into a new cJSON object (caller owns/frees it).
 * Used by both the real fetch path and the demo path so they can't drift
 * apart on which fields get written (see ADR 0003). */
cJSON *serialize_weather_info(const WeatherInfo *info);
