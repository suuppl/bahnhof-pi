---
status: accepted
---

> Note: the solar/battery parts referenced below were later moved out of this repository; the bottom bar is now plugin-driven (see [`../plugins.md`](../plugins.md)).

# Collapse demo emit into the real serializer, per fetcher

Each fetcher's `--demo` path re-implements its own JSON serializer field-by-field instead of reusing the real one: `iris_fetch.c`'s `write_demo_board` (449-488) already dropped the `planned_platform`/`changed_platform` fields that `write_departures` (261-316) emits; `weather_fetch.c`'s demo path computes sunrise/sunset differently (`now±6h`, ~204-205) than the real path (~116-134). Decision: once fetchers build a typed value ([0001](0001-typed-departure-value.md)), both the real fetch path and the demo path call the same `serialize_*_info()` function, so the two can't drift apart again.

**Explicit no**: the canned demo *data* itself (board/case tables, e.g. `iris_fetch.c:369-445`, `weather_fetch.c`'s `now±6h` sunrise/sunset) stays per-daemon and hand-written, including where it's intentionally synthetic/fake — that's not the bug this ADR fixes. Same for the per-daemon demo loop boilerplate (mkdir/banner/`for(;;)+sleep`) — not worth a shared abstraction for ~8 lines × 3.

## Decisions

- **`DepartureInfo`/`WeatherInfo`/`SolarInfo` grow to full canonical records**: even fields no current consumer reads (e.g. `planned_platform`/`changed_platform`, which `display.c` never renders) belong on the struct, so one serializer can reproduce the complete JSON schema for both real and demo paths. `DepartureInfo` is "the complete value that crosses the IPC boundary," not "the subset the current consumer happens to use" — this is what makes the original bug (demo silently dropping a field) structurally harder: you're filling in one struct instead of hand-writing two parallel `cJSON_Add*` call lists.
- **Serializers live alongside parsers**: `serialize_departure_info()`/`serialize_weather_info()`/`serialize_solar_info()` go in the same per-domain files from 0001 (`departure_info.c`, `weather_info.c`, `solar_info.c`), taking the canonical struct and returning/populating a `cJSON*`.
- **Producer-side conversions stay private**: `Departure`→`DepartureInfo` (real path) and `DemoDep`→`DepartureInfo` (demo path) conversions stay in `iris_fetch.c` — they're specific to how each path builds its data (XML-parsed vs. canned table) and aren't shared concerns. Only the canonical struct and its serializer are shared.
