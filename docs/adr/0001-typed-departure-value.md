---
status: accepted
---

> Note: the solar/battery parts referenced below were later moved out of this repository; the bottom bar is now plugin-driven (see [`../plugins.md`](../plugins.md)).

# Deepen the Departure/WeatherReading/SolarReading value — one parse, everywhere

`display.c` hand-parses cJSON inline in `update_departures()` (228-234), `update_weather()` (305-312), and `update_solar()` (345-348) — the same "read field X, cJSON_GetObjectItem, default to 0/NULL" pattern repeated at ~20 call sites, with missing and legitimately-zero fields indistinguishable everywhere. Decision: one parse function per record type returns a typed value; every consumer and every demo emitter takes that value instead of a cJSON tree.

This is the foundation candidate — it's what lets [0002](0002-departure-row-module.md)'s Row module take a single typed argument, and what lets [0003](0003-collapse-demo-serializers.md)'s demo/real serializers collapse into one function.

## Decisions

- **Shared header, one per domain**: `src/departure_info.h`/`.c`, `src/weather_info.h`/`.c`, `src/solar_info.h`/`.c` — kept separate per service rather than one consolidated module, matching this project's preference for domain separation. Each defines its `*Info` struct and a `parse_*_info()` function.
- **Distinct from producer-internal structs**: `iris_fetch.c`'s existing `Departure` (XML-sourced, carries producer-only fields like `id`/`hidden`) is untouched; the producer maps it into `DepartureInfo` right before serializing. No name collision, no bloating the shared type with fields the JSON never carries.
- **Absent vs. legitimately-zero**: numeric fields use `NAN` as the "field absent" sentinel (checked via `isnan()`), reusing what `cJSON_GetNumberValue` already returns for a missing/non-number item — no separate presence flags. Only applies to `double` fields; ints/chars (e.g. `delay_min`) have no such ambiguity in practice.
- **Signature**: `DepartureInfo parse_departure_info(const cJSON *dep);` — returned by value, no bool/out-param split, since there's no real "parse failure" case short of malformed JSON (already caught earlier by `cJSON_Parse` returning `NULL`). String fields are fixed-size buffers copied out of the tree during parsing (required regardless, since `cJSON_GetObjectItem` pointers are owned by the tree and freed on `cJSON_Delete`).
- **Derived fields baked in for `DepartureInfo`**: `effective_ts` (planned-vs-changed picking logic) and `cancelled` (from the status char) are computed once inside `parse_departure_info()`, not re-derived by each consumer — this directly targets the drift [0002](0002-departure-row-module.md) already found between `update_departures()` and `init_dummy_data()`. `WeatherInfo`/`SolarInfo` have no equivalent derived logic today, so they stay thin mirrors of their JSON fields.
