#pragma once

#include <stdbool.h>
#include <time.h>
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

    /* derived once by departure_info_derive(), not re-derived by each
     * consumer (see ADR 0001/0002) */
    bool   cancelled;             /* status == 'c' */
    bool   show_delay;            /* not cancelled, changed_ts set, |delay_min| >= threshold */
    double effective_ts;          /* the time the board shows: changed_ts if show_delay, else planned_ts */
} DepartureInfo;

/* Fill the derived fields from the raw ones. Delays smaller than
 * delay_threshold_min count as on time, so effective_ts is the planned
 * time — the same time producer (filter) and display (countdown) use. */
void departure_info_derive(DepartureInfo *info, int delay_threshold_min);

/* True once the shown time is more than grace_s seconds in the past. */
bool departure_info_departed(const DepartureInfo *info, time_t now, int grace_s);

/* Parse one departure JSON object (an element of the "departures" array)
 * into a typed value (derived fields via departure_info_derive()). Never fails short of malformed JSON, which is already
 * caught earlier by cJSON_Parse returning NULL. */
DepartureInfo parse_departure_info(const cJSON *dep, int delay_threshold_min);

/* Serialize a DepartureInfo into a new cJSON object (caller owns/frees it).
 * Used by both the real fetch path and the demo path so they can't drift
 * apart on which fields get written (see ADR 0003). */
cJSON *serialize_departure_info(const DepartureInfo *info);
