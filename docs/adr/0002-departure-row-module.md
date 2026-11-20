---
status: accepted
---

# Give the departure Row a module — collapse the dummy path into it

`init_dummy_data()` (display.c:494-544) is a second, hand-maintained implementation of row rendering that has already drifted from the real path (`update_departures()`, display.c:211-289): it uses the old cancelled-color scheme instead of grey+strikethrough, never applies the S8 yellow-pill-text rule, and writes the pre-refactor HH:MM string into what is now the minutes-until-departure label. Decision: a `DepartureRow` struct + a pair of functions own row creation and rendering; the placeholder board becomes canned `DepartureInfo` values fed through the same render call, so the divergent copy disappears rather than needing to be kept in sync by hand.

Depends on [0001](0001-typed-departure-value.md) (render needs a typed `DepartureInfo` value to take as its argument, not a cJSON tree).

## Decisions

- **Extracted within `display.c`, not a separate file**: unlike 0001's IPC types (pure data, cheap to split per-domain), the Row module is LVGL widget code tightly coupled to `display.c`-local statics — fonts (`font_row`, `font_col`), colors (`C_TEXT`, `C_DIM`, `C_DELAY`), layout constants (`COL_TIME`, `COL_CLOCK`, `COL_TRAIN`), and helpers (`make_row`, `make_label`, `set_text_color`, `train_pill_color`, `train_pill_text_color`). Splitting to its own file would mean leaking all of that into a header or threading it through every call as parameters — pure overhead, since `display.c` is the only caller. This isn't a cross-service boundary like iris/weather/solar; it's intra-file UI structure.
- **Naming**: `DepartureRow` struct (replaces the current anonymous `g_rows[]` struct type), matching the `DepartureInfo` convention from 0001.
- **API**: `DepartureRow departure_row_create(lv_obj_t *parent, uint32_t bg_color);` (returned by value, replaces the inline widget-building block in the `MAX_ROWS` pre-allocation loop) and `void departure_row_render(DepartureRow *row, DepartureInfo info);` (replaces both the field-setting block in `update_departures()` and all of `init_dummy_data()`'s row loop).
- **Placeholder data stays local**: `init_dummy_data()` is the startup placeholder shown before any real `departures.json` exists (gated by `g_has_real_data`) — a different concept from each fetcher's `--demo` CLI flag (scoped to [0003](0003-collapse-demo-serializers.md)). It doesn't need any dependency on `iris_fetch`'s demo serialization; it just builds 8 `DepartureInfo` struct literals directly and calls `departure_row_render()` once per row.
