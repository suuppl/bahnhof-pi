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

`patches/lvgl-*.patch` are applied to the `lvgl/` submodule checkout by
`CMakeLists.txt` at configure time (skipped when already applied), so every
build — cross, native and Docker — gets them. Marked `/* Local patch ... */`:

- **`src/drivers/display/fb/lv_linux_fbdev.c`** — `flush_cb` calls
  `ioctl(fbfd, FBIO_WAITFORVSYNC, ...)` before writing pixels, to avoid
  tearing on the Pi's single-buffer `vc4drmfb` (`-1` harmlessly where unsupported).
- **`src/libs/freetype/lv_freetype_glyph.c`** and
  **`src/libs/freetype/lv_freetype_image.c`** — **color emoji**. Stock LVGL's
  FreeType glue renders `Twemoji.Mozilla.ttf` (COLR) glyphs as empty A8
  bitmaps, i.e. the bar emoji vanish. For `FT_HAS_COLOR` faces the patch loads
  with `FT_LOAD_COLOR`, marks the glyph `LV_FONT_GLYPH_FORMAT_IMAGE` and
  converts FreeType's premultiplied BGRA to an `ARGB8888` draw buffer (needs
  `LV_USE_IMGFONT 1`).

To change a patch: edit the files in `lvgl/`, then
`git -C lvgl diff > patches/lvgl-local.patch`. If LVGL is bumped and a patch
no longer applies, cmake stops with an error.

## Configuration

All compile-time options are in [`lv_conf.h`](index.md#lv_confh) at the project root. The CMake target is told to look for it via:

```cmake
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_include_directories(lvgl PUBLIC ${CMAKE_SOURCE_DIR} …)
```

`LV_CONF_INCLUDE_SIMPLE` makes LVGL include `lv_conf.h` by name (without a path), so it picks up the one in `CMAKE_SOURCE_DIR`.
