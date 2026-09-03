# Task 4: Codec interface and detection — Report

## Status: DONE

## Commit
`3e7cfed` — "Add codec interface, magic detection and lookup" (master, in place, no worktree)

## Files created
- `src/core/codec.h` — `CodecId`, `Codec` abstract interface, `DetectCodec`,
  `CodecName`, `ParseCodecName`, `FindCodec`, `IsCodecAvailable`,
  `AvailableCodecNames`, and the guarded `ZstdCodec()`/`SevenZipCodec()`
  declarations (`RCEDIT_HAS_ZSTD` / `RCEDIT_HAS_7Z`).
- `src/core/codec.cpp` — implementation: magic-byte detection (7z: `37 7A BC
  AF 27 1C`, zstd: `28 B5 2F FD`), name lookup, `FindCodec` dispatch guarded
  by the same macros, `AvailableCodecNames` ("none" first).
- `tests/test_codec.cpp` — the `codec` test group, per the brief, with the
  R18 change (see below).

## Files modified
- `src/core/CMakeLists.txt` — added `codec.h codec.cpp` to
  `RCEDIT_CORE_SOURCES`.
- `tests/CMakeLists.txt` — added `test_codec.cpp` to the `rcedit_tests`
  sources and `codec` to `RCEDIT_TEST_GROUPS`.
- `tests/main.cpp` — declared and registered `RunCodecTests` in `kGroups`;
  trimmed the "later tasks" comment now that codec is done.

## TDD evidence
1. Wrote `tests/test_codec.cpp` and added `codec.h`/`codec.cpp` to the
   CMake source lists before the headers/implementation existed. Building
   `minimal-MinSizeRel` at that point failed exactly as expected, with the
   compiler unable to find `core/codec.h` (`fatal error C1083: Cannot open
   include file: 'core/codec.h'`) — confirms the test-first step actually
   exercised a red state referencing the real header path used by
   `#include "core/codec.h"`.
2. Added `src/core/codec.h` and `src/core/codec.cpp`. Rebuilt: full success,
   0 warnings under `/W4 /WX /guard:cf /EHsc /sdl /utf-8`.
3. Ran `ctest --preset minimal-MinSizeRel`: `codec` group passes, along with
   all pre-existing groups and the `imports` allowlist check.
4. Ran `rcedit_tests.exe codec` directly to confirm exactly the four
   detection/lookup cases run under `minimal` (round-trip cases compiled out
   since neither `RCEDIT_HAS_ZSTD` nor `RCEDIT_HAS_7Z` is defined):
   ```
   [codec] DetectsMagics
   [codec] NamesRoundTrip
   [codec] FindCodecMatchesBuildFlags
   [codec] AvailableNamesListOnlyCompiledCodecs
   codec: 4 case(s), 0 failure(s)
   ```

## Build + test commands and results (minimal preset — gating)
```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64
cmake --preset minimal
cmake --build --preset minimal-MinSizeRel
ctest --preset minimal-MinSizeRel
```
Build output: clean, 5 build steps, no warnings/errors.

CTest output:
```
Test project S:/llm/rcedit/build/minimal
    Start 1: version
1/6 Test #1: version ..........................   Passed    0.04 sec
    Start 2: error
2/6 Test #2: error ............................   Passed    0.01 sec
    Start 3: encoding
3/6 Test #3: encoding .........................   Passed    0.01 sec
    Start 4: resource_id
4/6 Test #4: resource_id ......................   Passed    0.01 sec
    Start 5: codec
5/6 Test #5: codec ............................   Passed    0.01 sec
    Start 6: imports
6/6 Test #6: imports ..........................   Passed    0.05 sec

100% tests passed, 0 tests failed out of 6
```
Pristine — no warnings, all tests pass, `/WX` clean. Re-verified after
`clang-format -i` (rebuild + retest, identical clean result).

## `default` preset (7z + zstd enabled) — attempted per instructions
```powershell
cmake --preset default
cmake --build --preset default-MinSizeRel
```
Result: `src/core/codec.cpp` compiled cleanly, `rcedit_core.lib` linked, and
`rcedit.exe` linked and ran (it doesn't reference the guarded codec
factories). `rcedit_tests.exe` **failed at link time** with:
```
test_codec.cpp.obj : error LNK2019: unresolved external symbol
  "class rcedit::Codec & __cdecl rcedit::ZstdCodec(void)" ...
rcedit_core.lib(codec.cpp.obj) : error LNK2001: unresolved external symbol
  "class rcedit::Codec & __cdecl rcedit::ZstdCodec(void)"
test_codec.cpp.obj : error LNK2019: unresolved external symbol
  "class rcedit::Codec & __cdecl rcedit::SevenZipCodec(void)" ...
rcedit_core.lib(codec.cpp.obj) : error LNK2001: unresolved external symbol
  "class rcedit::Codec & __cdecl rcedit::SevenZipCodec(void)"
```
This is the **expected** situation per the task instructions: the
`#ifdef RCEDIT_HAS_ZSTD` / `#ifdef RCEDIT_HAS_7Z` branches in `codec.cpp`
and `test_codec.cpp` compile successfully — proving the guarded code is
syntactically and semantically correct — but `ZstdCodec()`/`SevenZipCodec()`
have no definitions until Tasks 5 and 6 add them. No stub definitions were
added, as instructed.

## Ruling R10 (EqualsIgnoreCase)
`codec.cpp` does **not** reimplement a case-fold loop. It includes
`core/encoding.h` and calls the shared `rcedit::EqualsIgnoreCase` in
`ParseCodecName`:
```cpp
for( const CodecId id : { CodecId::None, CodecId::SevenZip, CodecId::Zstd } ) {
    if( EqualsIgnoreCase( name, CodecName( id ) ) ) {
        return id;
    }
}
```
Note: the brief's literal reference `codec.cpp` (Step 3 code block)
contained its own local `EqualsIgnoreCase` helper using
`std::towlower`/`std::equal` in an anonymous namespace — this was **not**
copied. It was replaced with a call to the already-established
`rcedit::EqualsIgnoreCase` from `src/core/encoding.h`, and the
now-unnecessary `<cwctype>` include from the brief's reference was dropped
(replaced by `#include "core/encoding.h"`). This avoids the duplicate
case-fold implementation flagged by the pre-flight scan.

## Ruling R18 (specific error code for `FindCodec(CodecId::None)`)
`FindCodec`'s documented contract (see `codec.h` comment: "errc::codec_disabled
for a codec compiled out; std::errc::invalid_argument for None.") and its
implementation both resolve `CodecId::None` to
`std::make_error_code( std::errc::invalid_argument )`. The
`FindCodecMatchesBuildFlags` test was changed from the brief's
`CHECK(!none.has_value());` to assert the exact code:
```cpp
auto none = FindCodec( CodecId::None );
CHECK(
    !none.has_value()
    && none.error()
        == std::make_error_code( std::errc::invalid_argument ) );
```
This is stronger than the brief's literal test and was verified to pass
under `minimal`.

## Concerns
None blocking. Two notes for the record:
- The `default` preset's test binary cannot fully link until Task 5 (zstd)
  and Task 6 (7z) land — this is the designed, expected sequencing, not a
  defect in Task 4's code.
- `rcedit.exe` itself already builds/links successfully under `default`
  today because it doesn't currently reference `ZstdCodec`/`SevenZipCodec`;
  this will change once later tasks wire the CLI to the codec registry, at
  which point `rcedit.exe` will also depend on Tasks 5/6 landing before
  `default` links end-to-end. No action needed for Task 4.
