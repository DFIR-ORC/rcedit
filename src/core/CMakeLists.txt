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
)

add_library(rcedit_core STATIC ${RCEDIT_CORE_SOURCES})

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
