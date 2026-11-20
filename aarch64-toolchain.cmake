set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(SYSROOT "${CMAKE_CURRENT_LIST_DIR}/sysroot")

# Tell the linker/compiler where to find Pi headers and libs
set(CMAKE_SYSROOT "")   # don't restrict includes, we add paths explicitly
set(CMAKE_FIND_ROOT_PATH "${SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Allow transitive shared-lib deps to be missing locally; they exist on Pi
set(CMAKE_EXE_LINKER_FLAGS "-Wl,--allow-shlib-undefined" CACHE STRING "" FORCE)

# Make these available to CMakeLists.txt (CACHE so they survive toolchain scope)
set(CROSS_INCLUDE_DIRS
    "${SYSROOT}/include"
    "${SYSROOT}/include/libxml2"
    "${SYSROOT}/include/curl"
    "${SYSROOT}/include/freetype2"
    "${SYSROOT}/include/cjson"
    CACHE STRING "Sysroot include dirs" FORCE)
set(CROSS_LIB_DIR "${SYSROOT}/lib/aarch64-linux-gnu"
    CACHE PATH "Sysroot lib dir" FORCE)
