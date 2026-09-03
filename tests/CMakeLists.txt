#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2026 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl (ANSSI)
#
add_executable(rcedit_tests
    check.h
    context.h
    temp.h
    main.cpp
    test_version.cpp
    test_error.cpp
    test_encoding.cpp
    test_resource_id.cpp
    test_codec.cpp
    test_engine.cpp
)
target_link_libraries(rcedit_tests PRIVATE rcedit_core)
target_include_directories(rcedit_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(RCEDIT_ENABLE_7Z)
    # test_sevenzip.cpp unit-tests OutMemStream directly (guarded by
    # RCEDIT_HAS_7Z), so it includes core/sevenzip/out_mem_stream.h and,
    # transitively, <7zip/7zip.h>. rcedit_core links 7zip PRIVATE, so its
    # include dirs do not propagate here; expose the overlay port's top-level
    # header root to this target the same way src/core does. The OutMemStream
    # symbols themselves come from rcedit_core.lib, so no extra link is needed.
    target_sources(rcedit_tests PRIVATE test_sevenzip.cpp)
    find_package(7zip CONFIG REQUIRED)
    # 7zip::7zip exposes <prefix>/include/7zip/CPP (needed by extras.h's
    # "7zip/IPassword.h" etc.); the find_path adds <prefix>/include itself for
    # the overlay's top-level <7zip/7zip.h>. Both mirror src/core/CMakeLists.
    target_link_libraries(rcedit_tests PRIVATE 7zip::7zip 7zip::extras)
    find_path(RCEDIT_7ZIP_ROOT_INCLUDE_DIR "7zip/7zip.h" REQUIRED)
    target_include_directories(rcedit_tests PRIVATE "${RCEDIT_7ZIP_ROOT_INCLUDE_DIR}")
endif()

if(RCEDIT_ENABLE_ZSTD)
    # test_codec.cpp drives the raw zstd streaming API (guarded by
    # RCEDIT_HAS_ZSTD) to build a frame without an embedded content size, to
    # cover ZstdCodecImpl::Decompress's streaming branch. rcedit_core links
    # zstd::libzstd_static PRIVATE, so its usage requirements (include dirs)
    # do not propagate here; link it directly for the test target too. This
    # does not affect the "imports" check, which inspects rcedit.exe only.
    find_package(zstd CONFIG REQUIRED)
    target_link_libraries(rcedit_tests PRIVATE zstd::libzstd_static)
endif()

add_executable(rcedit_fixture fixture/fixture.c fixture/fixture.rc)
add_dependencies(rcedit_tests rcedit_fixture)
# MSVC's linker embeds a default manifest (RT_MANIFEST #1, lang 1033) unless
# suppressed; the fixture's baseline must contain only its RCDATA resource.
target_link_options(rcedit_fixture PRIVATE /MANIFEST:NO)

# One CTest per group. Later tasks append to RCEDIT_TEST_GROUPS.
set(RCEDIT_TEST_GROUPS version error encoding resource_id codec engine)
if(RCEDIT_ENABLE_7Z)
    list(APPEND RCEDIT_TEST_GROUPS sevenzip)
endif()
foreach(group IN LISTS RCEDIT_TEST_GROUPS)
    add_test(NAME ${group} COMMAND rcedit_tests ${group} $<TARGET_FILE:rcedit_fixture>)
endforeach()

# Import allowlist check on a target's real executable.
get_filename_component(_vc_bin "${CMAKE_LINKER}" DIRECTORY)
find_program(RCEDIT_DUMPBIN dumpbin HINTS "${_vc_bin}" REQUIRED)

function(rcedit_add_import_check target)
    set(allowed "KERNEL32.dll")
    if(RCEDIT_ENABLE_7Z)
        list(APPEND allowed "OLEAUT32.dll")
    endif()

    add_test(
        NAME imports
        COMMAND "${CMAKE_COMMAND}"
            "-DDUMPBIN=${RCEDIT_DUMPBIN}"
            "-DEXE=$<TARGET_FILE:${target}>"
            "-DALLOWED=${allowed}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_imports.cmake"
    )
endfunction()

rcedit_add_import_check(rcedit)
