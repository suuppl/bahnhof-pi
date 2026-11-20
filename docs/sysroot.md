# `sysroot/` — Cross-Compilation Sysroot

A minimal copy of the Raspberry Pi's (aarch64 Debian/DietPi) development headers and shared-library stubs. Used only on the x86-64 build host; never deployed to the Pi.

## Why it exists

Cross-compiling C against system libraries (`libcurl`, `libxml2`, `libcjson`) requires the target architecture's headers and `.so` link stubs. The sysroot provides these without needing a full Pi filesystem.

## Structure

```
sysroot/
├── include/          # Header files copied from the Pi
│   ├── curl/         # libcurl headers
│   ├── libxml2/      # libxml2 headers
│   ├── cjson/        # cJSON headers
│   └── freetype2/    # FreeType (pulled in transitively)
├── lib/
│   └── aarch64-linux-gnu/   # aarch64 .so stubs
└── usr/              # Additional system headers (if needed)
```

## How it is used

`aarch64-toolchain.cmake` sets:

```cmake
set(CROSS_INCLUDE_DIRS
    "${SYSROOT}/include"
    "${SYSROOT}/include/libxml2"
    "${SYSROOT}/include/curl"
    "${SYSROOT}/include/cjson"
    …)
set(CROSS_LIB_DIR "${SYSROOT}/lib/aarch64-linux-gnu")
```

`CMakeLists.txt` reads these variables and passes them to `find_library()` and `target_include_directories()`.

## Transitive dependencies

The linker flag `-Wl,--allow-shlib-undefined` is set in the toolchain file. This allows linking to succeed even when transitive `.so` dependencies (e.g. `libssl` needed by `libcurl`) are absent from the local sysroot — they exist on the Pi at runtime.

## Updating the sysroot

Copy updated headers/stubs from the Pi:

```sh
rsync -av --include='*.h' --include='*.so*' --include='*/' --exclude='*' \
    pi:/usr/include/curl/ sysroot/include/curl/
rsync -av pi:/usr/lib/aarch64-linux-gnu/libcurl.so* sysroot/lib/aarch64-linux-gnu/
# repeat for libxml2, libcjson, etc.
```
