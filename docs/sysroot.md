# `sysroot/` — Cross-Compilation Sysroot

The aarch64 headers and shared libraries the cross build links against:
libcurl, libxml2, cJSON and FreeType. Used only on the build host, never
deployed. It is **not in git**: `make sysroot` copies it from your own Pi, so
the versions always match what runs there.

## Creating it

```sh
# once, on the Pi:
sudo apt install libcurl4-openssl-dev libxml2-dev libcjson-dev libfreetype-dev

# on the build host (uses PI / SSH_KEY from local.mk, like make deploy):
make sysroot
```

`make build` runs `make sysroot` automatically when `sysroot/` is missing.
Run it again after upgrading those libraries on the Pi. The copy goes to
`sysroot.new/` first, so a failed fetch keeps the old sysroot. If a `-dev`
package is missing on the Pi, it stops with the `apt install` command to run.

## Structure

A mirror of the relevant parts of the Pi's `/usr`:

```
sysroot/
└── usr/
    ├── include/
    │   ├── aarch64-linux-gnu/curl/   # libcurl headers (Debian multiarch path)
    │   ├── libxml2/
    │   ├── cjson/
    │   └── freetype2/
    └── lib/aarch64-linux-gnu/
        └── lib{curl,xml2,cjson,freetype}.so*
```

Nothing else is needed: the transitive dependencies (OpenSSL, zlib, nghttp2,
…) are not required at link time (`-Wl,--allow-shlib-undefined` in the
toolchain file) and come from the Pi at runtime. The sysroot has no libc
headers, so it can't shadow the cross compiler's own.

## How it is used

`aarch64-toolchain.cmake` sets `CROSS_INCLUDE_DIRS` (the include dirs above)
and `CROSS_LIB_DIR` (`sysroot/usr/lib/aarch64-linux-gnu`); `CMakeLists.txt`
passes them to `find_library()` and `target_include_directories()`. It fails
early with a hint if the sysroot is missing.

## Without a Pi at hand

Any directory with the same layout works: `make build SYSROOT=/path/to/sysroot`. For example,
extract the arm64 Debian packages (`libcurl4-openssl-dev`, `libcurl4`,
`libxml2-dev`, `libxml2`, `libcjson-dev`, `libcjson1`, `libfreetype-dev`,
`libfreetype6`) with `dpkg -x <pkg>.deb /path/to/sysroot`. Use the same
Debian release as the Pi.

## glibc compatibility

The C library comes from the cross compiler (`gcc-aarch64-linux-gnu`), not
from the sysroot. If the build host's distribution ships a newer glibc than
the Pi, binaries may fail on the Pi with `GLIBC_2.xx not found`; then build
on a host with the Pi's Debian release (e.g. in a container) or natively on
the Pi (`make build-native`).
