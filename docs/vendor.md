# `vendor/` — Vendored Third-Party Source

| Directory | Status | Notes |
|-----------|--------|-------|
| `cjson/` | Empty placeholder | cJSON is sourced from the Pi sysroot (`sysroot/include/cjson/`, `sysroot/lib/aarch64-linux-gnu/libcjson.so`), not built from source |

## cJSON

[cJSON](https://github.com/DaveGamble/cJSON) is a lightweight JSON parser used by both `iris_fetch` and `weather_fetch` for serialising output, and by `display.c` for reading the IPC JSON files.

If you ever want to build cJSON from source (e.g. to pin a specific version), clone it into `vendor/cjson/` and add an `add_subdirectory(vendor/cjson)` to `CMakeLists.txt`.
