#pragma once

#include <stdbool.h>
#include <cjson/cJSON.h>

/* The complete value that crosses the departures.json IPC boundary — every
 * field the schema carries, not just the subset display.c currently renders,
 * so one serializer can reproduce the full JSON for both the real fetch path
 * and each fetcher's --demo path (see ADR 0003). Distinct from iris_fetch.c's
 * own internal, XML-sourced Departure struct, which carries producer-only
 * bookkeeping fields (id, hidden, ...) this type doesn't need.
 *
 * Numeric fields absent from the source JSON parse to NAN (matching what
 * cJSON_GetNumberValue already returns for a missing/non-number item) rather
 * than a separate presence flag — check with isnan(). planned_ts/changed_ts
 * are the exception: the JSON always writes 0 for "no changed time", which
 * is unambiguous since a real epoch timestamp is never near 0. */
typedef struct {
    char   planned_time[8];       /* DEP_PLANNED_TIME, "HH:MM" or "" */
    char   changed_time[8];       /* DEP_CHANGED_TIME, "HH:MM" or "" */
    double planned_ts;            /* DEP_PLANNED_TS, unix epoch seconds */
    double changed_ts;            /* DEP_CHANGED_TS, 0 if none */
    char   train[32];             /* DEP_TRAIN, e.g. "S1" */
    char   destination[192];      /* DEP_DESTINATION */
    char   planned_platform[16];  /* DEP_PLANNED_PLATFORM */
    char   changed_platform[16];  /* DEP_CHANGED_PLATFORM */
    char   status;                /* DEP_STATUS: 'p'=planned, 'a'=added, 'c'=cancelled */
    int    delay_min;             /* DEP_DELAY_MIN, 0 if none */

    /* derived once here, not re-derived by each consumer (see ADR 0001/0002) */
    double effective_ts;          /* changed_ts if reported and off-schedule, else planned_ts */
    bool   cancelled;             /* status == 'c' */
} DepartureInfo;

/* Parse one departure JSON object (an element of the "departures" array)
 * into a typed value. Never fails short of malformed JSON, which is already
 * caught earlier by cJSON_Parse returning NULL. */
DepartureInfo parse_departure_info(const cJSON *dep);

/* Serialize a DepartureInfo into a new cJSON object (caller owns/frees it).
 * Used by both the real fetch path and the demo path so they can't drift
 * apart on which fields get written (see ADR 0003). */
cJSON *serialize_departure_info(const DepartureInfo *info);
