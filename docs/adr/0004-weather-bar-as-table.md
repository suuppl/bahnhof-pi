---
status: accepted
---

> Superseded: the bottom bar is now built from plugin files (see [`../plugins.md`](../plugins.md)); the weather groups moved into `weather_fetch` as the sample plugin and `WEA_GROUPS[]` no longer exists.

# Turn the weather-bar groups into a data table, not unrolled code

The five weather/solar groups (temp, uv, rain, solar, battery) in `display.c:785-847` are each built by a hand-inlined block following an identical shape (outer column → top subrow with emoji + fixed-width value label → sub subrow with 0-2 arrow+fixed-width-value pairs); the only variation is the emoji, the widget-handle target(s), whether/how many arrow-prefixed sub-values there are, and per-label magic pixel widths whose justification lives only in prose comments. Decision: collapse the five blocks into a `WEA_GROUPS[]` table (emoji, top width + target, 0-2 sub-slots each with optional arrow glyph + width + target) plus one render loop over the existing `make_wea_outer`/`make_wea_subrow` helpers, so the width-tuning knobs sit next to each other instead of interleaved with LVGL calls.

Independent of the other ADRs in this batch — purely a display.c-local refactor.

## Decisions

- **Table drives construction only.** The loop assigns results into the existing named globals (`wc_temp`, `wc_tempmin`, `solar_w_lbl`, `bat_soc_lbl`, ...) via `lv_obj_t **target` pointers in the table entries. `update_weather()`, `update_solar()`, and `init_dummy_data()` keep referencing those globals by name — unchanged. The ADR's problem is specifically the construction code (magic widths interleaved with LVGL calls); the ~15-20 consumer call sites are already clear and un-duplicated, so converting them to array-indexing would trade a clear name for a table index at the one place readability matters, for no corresponding benefit.
- **Sub-slots are a fixed-size array of 0-2** (`{ arrow, target, width }`, empty `arrow`/`NULL` target = unused slot) rather than a variable-length list — the current five groups only ever need 0, 1, or 2, and a fixed cap keeps the render loop simple (`for (int s = 0; s < 2 && grp.sub[s].target; s++)`).
