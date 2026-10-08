#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2026 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl (ANSSI)
#
cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

option(RCEDIT_ENABLE_7Z "Build the 7z codec (links 7-Zip statically)" ON)
option(RCEDIT_ENABLE_ZSTD "Build the zstd codec (links zstd statically)" ON)
option(RCEDIT_BUILD_TESTS "Build and register tests" ON)

# vcpkg must be wired before project() so the toolchain sees it.
set(RCEDIT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT DEFINED VCPKG_ROOT)
    set(VCPKG_ROOT "${RCEDIT_ROOT}/external/vcpkg")
endif()
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "")
endif()
set(VCPKG_OVERLAY_PORTS "${RCEDIT_ROOT}/external/vcpkg_overlay_ports")
set(VCPKG_OVERLAY_TRIPLETS "${RCEDIT_ROOT}/external/vcpkg_overlay_triplets")
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
endif()

set(VCPKG_MANIFEST_FEATURES "")
if(RCEDIT_ENABLE_7Z)
    list(APPEND VCPKG_MANIFEST_FEATURES "7z")
endif()
if(RCEDIT_ENABLE_ZSTD)
    list(APPEND VCPKG_MANIFEST_FEATURES "zstd")
endif()

project(rcedit VERSION 2.0.0 LANGUAGES C CXX RC)

if(NOT MSVC)
    message(FATAL_ERROR "rcedit requires MSVC")
endif()
if(BUILD_SHARED_LIBS)
    message(FATAL_ERROR "rcedit is static only: BUILD_SHARED_LIBS must be OFF")
endif()
# Only the codecs pull vcpkg ports in. The static CRT itself comes from
# CMAKE_MSVC_RUNTIME_LIBRARY below and from src/cli's explicit /NODEFAULTLIB
# link line, neither of which depends on the triplet, so a codec-less build
# has nothing left to conflict with: a parent project embedding rcedit for
# its resource editing alone may use whatever triplet it needs.
if(RCEDIT_ENABLE_7Z OR RCEDIT_ENABLE_ZSTD)
    if(NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static")
        message(FATAL_ERROR "rcedit codecs are static only: triplet must be x64-windows-static (got ${VCPKG_TARGET_TRIPLET})")
    endif()
endif()

get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_is_multi_config AND PROJECT_IS_TOP_LEVEL)
    # Ninja Multi-Config's built-in default omits MinSizeRel; the presets
    # build and test that configuration, so declare it explicitly. Only when
    # rcedit drives the build: this is a FORCE write to the cache, and a
    # parent project's own configuration types must win over ours.
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;MinSizeRel" CACHE STRING "" FORCE)
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ninja Multi-Config nests target binary dirs under each subdirectory; force
# every executable/DLL output to a flat build/<preset>/<Config>/ directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

add_compile_options(/W4 /WX /guard:cf /EHsc /sdl /utf-8 /external:W0 /Zi)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
add_link_options(/guard:cf)

add_subdirectory(src)

if(RCEDIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
