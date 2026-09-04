#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright 2026 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl (ANSSI)
#
# Usage: cmake -DRCEDIT=<exe> -DFIXTURE=<exe> -DWORKDIR=<dir> [-DCODEC=zstd] -P cli_smoke.cmake
# Drives the built rcedit.exe through set/list/get/hexdump/remove on a copy of the fixture.

if(NOT RCEDIT OR NOT FIXTURE OR NOT WORKDIR)
    message(FATAL_ERROR "cli_smoke.cmake needs RCEDIT, FIXTURE and WORKDIR")
endif()

file(REMOVE_RECURSE "${WORKDIR}")
file(MAKE_DIRECTORY "${WORKDIR}")
set(PE "${WORKDIR}/target.exe")
file(COPY_FILE "${FIXTURE}" "${PE}")

# run(<expected_exit> <var_for_stdout> args...)
# Also exports the run's stderr as LAST_STDERR, so a caller can assert that a
# successful command stayed off stderr and that a usage block is untagged.
function(run expected outvar)
    execute_process(
        COMMAND "${RCEDIT}" ${ARGN}
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
    )
    if(NOT rc EQUAL expected)
        message(FATAL_ERROR "rcedit ${ARGN}\nexpected exit ${expected}, got ${rc}\nstdout:\n${out}\nstderr:\n${err}")
    endif()
    set(${outvar} "${out}" PARENT_SCOPE)
    set(LAST_STDERR "${err}" PARENT_SCOPE)
endfunction()

function(expect_empty text what)
    if(NOT text STREQUAL "")
        message(FATAL_ERROR "${what}: expected nothing, got:\n${text}")
    endif()
endfunction()

function(expect_match text pattern what)
    if(NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${what}: expected to match '${pattern}', got:\n${text}")
    endif()
endfunction()

function(expect_no_match text pattern what)
    if(text MATCHES "${pattern}")
        message(FATAL_ERROR "${what}: expected no match for '${pattern}', got:\n${text}")
    endif()
endfunction()

# usage errors
run(2 out)
run(2 out list)
run(2 out list "${PE}" --bogus)
# the message is tagged, the usage block that follows it is not
expect_match("${LAST_STDERR}" "\\[E\\] [^\n]*--bogus" "usage error message")
expect_match("${LAST_STDERR}" "(^|\n)Usage: rcedit list" "usage block untagged")
expect_empty("${out}" "usage error on stdout")
run(2 out set "${PE}" -t RT_RCDATA -n CONFIG)
run(0 out --version)
expect_match("${out}" "rcedit [0-9]+\\.[0-9]+\\.[0-9]+" "--version")
run(0 out --help)
expect_match("${out}" "Commands:" "--help")
run(0 out set --help)
expect_match("${out}" "--value-path" "set --help")

# baseline
run(0 out list "${PE}")
expect_match("${out}" "TYPE +NAME +LANG +SIZE +CODEC +UNPACKED +PREVIEW" "list header")
expect_no_match("${out}" "CONTENT" "list header no longer says CONTENT")
expect_match("${out}" "RT_RCDATA +FIXTURE +0 +7 +- +- +fixture" "list baseline")

# set / list / get / hexdump
run(0 out set "${PE}" -t RT_RCDATA -n CONFIG --value "<config/>")
expect_match("${out}" "Set resource in '[^']*target\\.exe'" "set header")
expect_match("${out}" "\n  Type +RT_RCDATA \\(#10\\)\n" "set type row")
expect_match("${out}" "\n  Name +CONFIG\n" "set name row")
expect_match("${out}" "\n  Lang +0 \\(neutral\\)\n" "set lang row")
expect_match("${out}" "\n  Size +9 bytes\n" "set size row")
expect_empty("${LAST_STDERR}" "set on stderr")

run(0 out list "${PE}")
expect_match("${out}" "RT_RCDATA +CONFIG +0 +9 +- +- +<config/>" "list after set")

run(0 out get "${PE}" -t RCDATA -n CONFIG -o "${WORKDIR}/config.bin")
expect_match("${out}" "Extracted resource from '[^']*target\\.exe'" "get header")
expect_match("${out}" "\n  Lang +0 \\(neutral\\)\n" "get lang row")
expect_match("${out}" "\n  Stored +9 bytes\n" "get stored row")
expect_match("${out}" "\n  Wrote +9 bytes to '[^']*config\\.bin'\n" "get wrote row")
expect_empty("${LAST_STDERR}" "get on stderr")
file(READ "${WORKDIR}/config.bin" content)
if(NOT content STREQUAL "<config/>")
    message(FATAL_ERROR "get returned '${content}'")
endif()

# --quiet silences the confirmation but never the data a command is asked for
run(0 out --quiet set "${PE}" -t RT_RCDATA -n QUIET --value "q")
expect_empty("${out}" "set --quiet on stdout")
expect_empty("${LAST_STDERR}" "set --quiet on stderr")
run(0 out --quiet list "${PE}" -n QUIET)
expect_match("${out}" "RT_RCDATA +QUIET" "list --quiet still prints its table")
run(0 out --quiet remove "${PE}" -t RT_RCDATA -n QUIET)
expect_empty("${out}" "remove --quiet on stdout")

run(0 out hexdump "${PE}" -t "#10" -n CONFIG)
expect_match("${out}" "00000000  3C 63 6F 6E 66 69 67 2F  3E .*\\|<config/>\\|" "hexdump")

run(0 out hexdump "${PE}" -t RT_RCDATA -n CONFIG --limit 4)
expect_match("${out}" "5 more bytes" "hexdump --limit")

# runtime errors exit 1
run(1 out get "${PE}" -t RT_RCDATA -n MISSING -o "${WORKDIR}/x.bin")
run(1 out list "${WORKDIR}/does-not-exist.exe")

# --output leaves the source untouched
run(0 out set "${PE}" -t RT_RCDATA -n OTHER --value-utf16 "ab" -o "${WORKDIR}/copy.exe")
# the header names the file actually written, not the source
expect_match("${out}" "Set resource in '[^']*copy\\.exe'" "set --output header")
run(0 out list "${PE}")
expect_no_match("${out}" "OTHER" "source after set --output")
run(0 out list "${WORKDIR}/copy.exe")
expect_match("${out}" "RT_RCDATA +OTHER +0 +4 +-" "copy after set --output")

# compression, when a codec is built in
if(CODEC)
    run(0 out set "${PE}" -t RT_RCDATA -n PACKED --value-path "${FIXTURE}" -c "${CODEC}")
    # a compressed payload splits the one Size row into Input and Stored
    expect_match("${out}" "\n  Input +[0-9]+ bytes\n" "set packed input row")
    expect_match("${out}" "\n  Stored +[0-9]+ bytes \\(${CODEC}\\)\n" "set packed stored row")
    expect_no_match("${out}" "\n  Size +" "set packed has no plain size row")
    run(0 out list "${PE}" -n PACKED)
    expect_match("${out}" "PACKED +0 +[0-9]+ +${CODEC} +[0-9]+" "list packed")
    run(0 out get "${PE}" -t RT_RCDATA -n PACKED -o "${WORKDIR}/unpacked.bin")
    expect_match("${out}" "\n  Stored +[0-9]+ bytes \\(${CODEC}\\)\n" "get packed stored row")
    file(SHA256 "${FIXTURE}" expected_hash)
    file(SHA256 "${WORKDIR}/unpacked.bin" actual_hash)
    if(NOT expected_hash STREQUAL actual_hash)
        message(FATAL_ERROR "decompressed payload differs from the input")
    endif()
    run(0 out get "${PE}" -t RT_RCDATA -n PACKED -o "${WORKDIR}/raw.bin" --raw)
    expect_match("${out}" "bytes to '[^']*raw\\.bin' \\(raw\\)" "get --raw notes the payload is untouched")
    file(SHA256 "${WORKDIR}/raw.bin" raw_hash)
    if(raw_hash STREQUAL expected_hash)
        message(FATAL_ERROR "--raw returned the decompressed payload")
    endif()
endif()

# remove
run(0 out remove "${PE}" -t RT_RCDATA -n CONFIG)
expect_match("${out}" "Removed resource from '[^']*target\\.exe'" "remove header")
expect_match("${out}" "\n  Type +RT_RCDATA \\(#10\\)\n" "remove type row")
expect_match("${out}" "\n  Lang +0 \\(neutral\\)\n" "remove lang row")
expect_empty("${LAST_STDERR}" "remove on stderr")
run(0 out list "${PE}")
expect_no_match("${out}" "CONFIG" "list after remove")
run(1 out remove "${PE}" -t RT_RCDATA -n CONFIG)

message(STATUS "cli smoke: OK")
