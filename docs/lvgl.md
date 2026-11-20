# `lvgl/` — LVGL Graphics Library

LVGL (Light and Versatile Graphics Library) v9.2.2, included as a git submodule.

This is upstream LVGL plus three **local patches**, kept as an uncommitted
working-tree diff inside the submodule (not a fork/branch) so they don't
drift silently — see "Local patches" below before assuming this checkout is
stock or reverting it.

## Role in this project

- Provides the rendering engine, widget system, and Linux framebuffer driver (`lv_linux_fbdev`).
- Built as a static library target `lvgl` via `add_subdirectory(lvgl)` in `CMakeLists.txt`.
- The display program (`bahnhofstafel`) links against it.

## Key drivers used

| Driver | Purpose |
|--------|---------|
| `lv_linux_fbdev` | Writes pixels directly to `/dev/fb0` |
| `LV_USE_FS_POSIX` | Wraps POSIX `fopen`/`fread` so FreeType can load font files from disk (drive `A:` = `/`) |
| `LV_USE_FREETYPE` | Runtime FreeType renderer for DejaVu Sans + Twemoji/NotoEmoji glyphs (`LV_USE_TINY_TTF` is off) |

## Local patches

Three files carry intentional, commented modifications (`/* Local patch: ... */`)
required for this project's hardware and fonts. They are **not** accidental
drift and should not be reverted:

- **`src/drivers/display/fb/lv_linux_fbdev.c`** — `flush_cb` now calls
  `ioctl(fbfd, FBIO_WAITFORVSYNC, ...)` before writing pixels, to avoid
  tearing on the Pi's single-buffer `vc4drmfb`. The ioctl constant isn't in
  mainline `linux/fb.h` so it's `#define`d locally; on drivers that don't
  support it the ioctl just returns `-1` harmlessly.
- **`src/libs/freetype/lv_freetype_glyph.c`** and
  **`src/libs/freetype/lv_freetype_image.c`** — add support for **color
  bitmap fonts** (CBDT/CBLC, e.g. NotoColorEmoji/Twemoji). Stock LVGL's
  FreeType glue only handles vector/grayscale glyphs
  (`FT_LOAD_NO_BITMAP`, `A8` format) and can't load or render color emoji
  strikes at all. The patch detects `FT_HAS_COLOR` faces, selects the
  nearest embedded bitmap strike (`FT_Select_Size`), loads with
  `FT_LOAD_COLOR`, and rasterizes to `ARGB8888` (scaling down from the
  strike's native size to the requested font size) instead of `A8`. This is
  what makes the weather condition emoji (`display.c`'s WMO-code → glyph
  mapping) render at all.

If LVGL is ever bumped to a newer upstream commit, these three patches need
to be re-applied/re-verified against the new source — check
`git -C lvgl diff` beforehand to recover the current patch content.

## Configuration

All compile-time options are in [`lv_conf.h`](index.md#lv_confh) at the project root. The CMake target is told to look for it via:

```cmake
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_include_directories(lvgl PUBLIC ${CMAKE_SOURCE_DIR} …)
```

`LV_CONF_INCLUDE_SIMPLE` makes LVGL include `lv_conf.h` by name (without a path), so it picks up the one in `CMAKE_SOURCE_DIR`.
