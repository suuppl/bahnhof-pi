# Bahnhofstafel

A Raspberry Pi–based departure board: four cooperating C programs poll external
data sources and render a live train-departure/weather display to an
HDMI monitor over a framebuffer.

## Language

**DepartureInfo** / **WeatherInfo**:
The typed value crossing the JSON IPC boundary for each of the data
files (`departures.json`, `weather.json`) — what a consumer
(`display.c`, and eventually each fetcher's demo emitter) parses the raw JSON
into and works with, instead of hand-walking a `cJSON` tree at each call
site. Distinct from a fetcher's own producer-internal struct (e.g.
`iris_fetch.c`'s `Departure`, built from XML and carrying producer-only
bookkeeping fields like `id`/`hidden`) — the producer maps its internal
struct into the shared `*Info` type right before serializing.
_Avoid_: Departure struct (ambiguous with `iris_fetch.c`'s internal type —
say "producer-internal `Departure`" vs. "`DepartureInfo`" when it matters).

**Effective timestamp** (`effective_ts` field on `DepartureInfo`):
The timestamp actually used for countdown/display purposes — the changed
(delayed/early) time if one was reported and differs from plan, otherwise
the originally planned time. Baked into `DepartureInfo` once during parsing
(not recomputed per consumer) specifically to stop the real-data and demo
paths from independently re-deriving it and drifting apart.

**DepartureRow**:
The struct + `departure_row_create()`/`departure_row_render()` function pair
that owns one row's LVGL widgets in `display.c` (the departure board's list
of train rows). Takes a `DepartureInfo` to render — used by both the real
data path and the startup placeholder, so they can't drift into different
rendering rules the way `init_dummy_data()` and `update_departures()` once
did.
_Avoid_: "Row" alone (ambiguous — this project also has row-shaped things in
the bottom plugin bar).
