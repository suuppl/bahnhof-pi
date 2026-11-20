cmake_minimum_required(VERSION 3.16)
project(bahnhofstafel C)
set(CMAKE_C_STANDARD 11)

# Put all executables at the top of the build dir, also when this project is
# add_subdirectory()d by a parent (the Makefile deploys from there).
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

# ── library resolution ────────────────────────────────────────────────────
# Cross-compile: CROSS_INCLUDE_DIRS / CROSS_LIB_DIR are set by the toolchain.
# Native build: fall back to find_package / pkg-config.

if(CROSS_LIB_DIR)
    # -- cross-compilation path --
    set(INC_DIRS ${CROSS_INCLUDE_DIRS})
    # re-resolve on every configure so a changed sysroot (path or layout)
    # never leaves stale library paths in an existing build/ cache
    unset(CURL_LIB CACHE)
    unset(XML2_LIB CACHE)
    unset(CJSON_LIB CACHE)
    unset(FREETYPE_LIB CACHE)
    find_library(CURL_LIB      NAMES curl      PATHS ${CROSS_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
    find_library(XML2_LIB      NAMES xml2      PATHS ${CROSS_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
    find_library(CJSON_LIB     NAMES cjson     PATHS ${CROSS_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
    find_library(FREETYPE_LIB  NAMES freetype  PATHS ${CROSS_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
    set(EXT_LIBS ${CURL_LIB} ${XML2_LIB} ${CJSON_LIB} m)
else()
    # -- native build path --
    find_package(CURL REQUIRED)
    find_package(LibXml2 REQUIRED)
    find_package(Freetype REQUIRED)
    find_library(CJSON_LIB cjson REQUIRED)
    set(INC_DIRS
        ${CURL_INCLUDE_DIRS}
        ${LIBXML2_INCLUDE_DIR}
        ${FREETYPE_INCLUDE_DIRS}
    )
    set(EXT_LIBS CURL::libcurl LibXml2::LibXml2 ${CJSON_LIB} m)
    set(FREETYPE_LIB Freetype::Freetype)
    set(DISP_CJSON ${CJSON_LIB})
endif()

# Optional directory holding a site-specific config_local.h (see src/config.h).
# Searched before src/, so a parent project can inject its own settings.
set(BAHNHOF_LOCAL_CONFIG_DIR "" CACHE PATH "Directory containing config_local.h")
if(BAHNHOF_LOCAL_CONFIG_DIR)
    list(PREPEND INC_DIRS ${BAHNHOF_LOCAL_CONFIG_DIR})
endif()

# Exported so a parent project (add_subdirectory) can build extra fetchers
# against the shared helpers: target_link_libraries(x PRIVATE bahnhof_common)
add_library(bahnhof_common STATIC src/http.c src/fileio.c src/settings.c src/screen.c)
target_link_libraries(bahnhof_common PUBLIC ${EXT_LIBS})
target_include_directories(bahnhof_common PUBLIC src ${INC_DIRS})
if(CROSS_LIB_DIR)
    target_link_directories(bahnhof_common PUBLIC ${CROSS_LIB_DIR})
endif()

# ── LVGL ─────────────────────────────────────────────────────────────────

add_subdirectory(lvgl)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${INC_DIRS})

# ── iris_fetch ────────────────────────────────────────────────────────────

add_executable(iris_fetch src/iris_fetch.c src/http.c src/fileio.c src/departure_info.c src/settings.c)
target_include_directories(iris_fetch PRIVATE src ${INC_DIRS})
target_link_libraries(iris_fetch PRIVATE ${EXT_LIBS})
if(CROSS_LIB_DIR)
    target_link_directories(iris_fetch PRIVATE ${CROSS_LIB_DIR})
endif()

# ── weather_fetch (plugin: weather bar + sunrise/sunset) ─────────────────────────────────────────────────────────

add_executable(weather_fetch src/weather_fetch.c src/http.c src/fileio.c src/weather_info.c src/settings.c)
target_include_directories(weather_fetch PRIVATE src ${INC_DIRS})
target_link_libraries(weather_fetch PRIVATE ${EXT_LIBS})
if(CROSS_LIB_DIR)
    target_link_directories(weather_fetch PRIVATE ${CROSS_LIB_DIR})
endif()

# ── brightness_ctl (plugin: DDC/CI backlight curve) ────────────────────────────────────────────────────────

add_executable(brightness_ctl src/brightness_ctl.c src/brightness_curve.c src/fileio.c src/settings.c src/screen.c)
target_include_directories(brightness_ctl PRIVATE src ${INC_DIRS})
target_link_libraries(brightness_ctl PRIVATE ${CJSON_LIB})
if(CROSS_LIB_DIR)
    target_link_directories(brightness_ctl PRIVATE ${CROSS_LIB_DIR})
endif()

# ── presence_ctl (plugin: radar → screen on/off) ──────────────────────────

add_executable(presence_ctl src/presence_ctl.c src/fileio.c src/settings.c src/screen.c)
target_include_directories(presence_ctl PRIVATE src ${INC_DIRS})
target_link_libraries(presence_ctl PRIVATE ${CJSON_LIB})
if(CROSS_LIB_DIR)
    target_link_directories(presence_ctl PRIVATE ${CROSS_LIB_DIR})
endif()

# ── bahnhofstafel (display) ───────────────────────────────────────────────

add_executable(bahnhofstafel src/main.c src/display.c src/departure_info.c src/fileio.c src/settings.c src/screen.c)
target_include_directories(bahnhofstafel PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR} ${INC_DIRS})
target_link_libraries(bahnhofstafel PRIVATE lvgl ${CJSON_LIB} ${FREETYPE_LIB} m)
if(CROSS_LIB_DIR)
    target_link_directories(bahnhofstafel PRIVATE ${CROSS_LIB_DIR})
    target_link_libraries(bahnhofstafel PRIVATE ${CJSON_LIB} ${FREETYPE_LIB})
endif()
