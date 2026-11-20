set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# sysroot/ is a git-ignored mirror of the Pi's headers + link libraries for
# libcurl, libxml2, cJSON and FreeType, laid out like the Pi's /usr.
# Create it with `make sysroot` (copies them from your Pi over SSH), or point
# BAHNHOF_SYSROOT at another directory with the same layout (docs/sysroot.md).
set(BAHNHOF_SYSROOT "${CMAKE_CURRENT_LIST_DIR}/sysroot" CACHE PATH "aarch64 sysroot (see docs/sysroot.md)")
set(SYSROOT "${BAHNHOF_SYSROOT}")
if(NOT EXISTS "${SYSROOT}/usr/lib/aarch64-linux-gnu/libcurl.so")
    message(FATAL_ERROR "No sysroot at ${SYSROOT} - run `make sysroot` (needs SSH access to the Pi), see docs/sysroot.md")
endif()

# Tell the linker/compiler where to find Pi headers and libs
set(CMAKE_SYSROOT "")   # don't restrict includes, we add paths explicitly
set(CMAKE_FIND_ROOT_PATH "${SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Allow transitive shared-lib deps to be missing locally; they exist on Pi
set(CMAKE_EXE_LINKER_FLAGS "-Wl,--allow-shlib-undefined" CACHE STRING "" FORCE)

# Make these available to CMakeLists.txt (CACHE so they survive toolchain scope).
# Only library-specific dirs: the sysroot holds no libc headers, so nothing
# shadows the cross compiler's own.
set(CROSS_INCLUDE_DIRS
    "${SYSROOT}/usr/include/aarch64-linux-gnu"   # Debian multiarch (curl)
    "${SYSROOT}/usr/include"
    "${SYSROOT}/usr/include/libxml2"
    "${SYSROOT}/usr/include/freetype2"
    "${SYSROOT}/usr/include/cjson"
    CACHE STRING "Sysroot include dirs" FORCE)
set(CROSS_LIB_DIR "${SYSROOT}/usr/lib/aarch64-linux-gnu"
    CACHE PATH "Sysroot lib dir" FORCE)
