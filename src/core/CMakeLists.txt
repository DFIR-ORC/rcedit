#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2026 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl (ANSSI)
#
set(RCEDIT_CORE_SOURCES
    version.h
    version.cpp
    error.h
    error.cpp
    log.h
    log.cpp
    output.h
    output.cpp
    encoding.h
    encoding.cpp
    format.h
    guard.h
    resource_id.h
    resource_id.cpp
    codec.h
    codec.cpp
)

add_library(rcedit_core STATIC ${RCEDIT_CORE_SOURCES})

if(RCEDIT_ENABLE_ZSTD)
    find_package(zstd CONFIG REQUIRED)
    target_sources(rcedit_core PRIVATE codec_zstd.h codec_zstd.cpp)
    # Static-only build (R6): the vcpkg zstd port for x64-windows-static
    # exports both zstd::libzstd_static (the real static IMPORTED library)
    # and zstd::libzstd (an INTERFACE shim that just forwards to it, kept
    # for source compatibility with shared-library consumers). Link the
    # static target directly rather than through the shim.
    target_link_libraries(rcedit_core PRIVATE zstd::libzstd_static)
endif()

target_include_directories(rcedit_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_compile_definitions(rcedit_core
    PRIVATE RCEDIT_VERSION="${PROJECT_VERSION}"
)

if(RCEDIT_ENABLE_7Z)
    target_compile_definitions(rcedit_core PUBLIC RCEDIT_HAS_7Z)
endif()
if(RCEDIT_ENABLE_ZSTD)
    target_compile_definitions(rcedit_core PUBLIC RCEDIT_HAS_ZSTD)
endif()
