# Task 3 report: Resource identifiers

## Status
DONE

## Commit
`89ccdcf` — "Add resource identifier parsing and formatting"

## Files created
- `src/core/resource_id.h`
- `src/core/resource_id.cpp`
- `tests/test_resource_id.cpp`

## Files modified
- `src/core/CMakeLists.txt` — added `resource_id.h resource_id.cpp` to `RCEDIT_CORE_SOURCES`.
- `src/core/format.h` — added `#include "core/resource_id.h"` and
  `std::formatter<rcedit::ResourceId, wchar_t>` specialization (only this
  formatter was added, per the accepted deviation — no formatters for
  `ResourceKey`, `std::error_code`, or `std::filesystem::path`).
- `tests/CMakeLists.txt` — added `test_resource_id.cpp` to the test
  executable's sources and `resource_id` to `RCEDIT_TEST_GROUPS`.
- `tests/main.cpp` — forward-declared and registered `RunResourceIdTests` in
  `kGroups`.

## TDD evidence

Step 1: wrote `tests/test_resource_id.cpp` (verbatim from the brief) and
wired it into `tests/main.cpp` / `tests/CMakeLists.txt` before any
implementation existed.

Step 2: built to confirm the expected failure:

```
cmake --build --preset minimal-MinSizeRel
...
S:\llm\rcedit\tests\test_resource_id.cpp(14): fatal error C1083: Cannot open
include file: 'core/resource_id.h': No such file or directory
```

This matches the brief's expected failure exactly.

Step 3: implemented `src/core/resource_id.h` / `.cpp` and the formatter in
`format.h`, per the brief's reference code — with one deliberate deviation
(see below).

Step 4: build + test, both green (details below).

## Deviation from the brief's reference code: EqualsIgnoreCase

The brief's reference `resource_id.cpp` includes its own local
`EqualsIgnoreCase` helper (using `std::towupper`/`<cwctype>`/`<algorithm>`).
Per the task instructions, I did **not** reproduce that duplicate — instead
`resource_id.cpp`:
- `#include`s `core/encoding.h` (Task 2) and drops `<algorithm>`/`<cwctype>`.
- Calls the shared `rcedit::EqualsIgnoreCase(std::wstring_view,
  std::wstring_view)` from `core/encoding.h` for both the `"RT_"` prefix
  check and the alias-table scan in `AliasToId`.

No local case-fold loop remains in `resource_id.cpp`. Everything else
(alias table of 21 entries, `ParseUnsigned`, `ParseResourceName`,
`ParseResourceType`, `ParseLang`, `FormatResourceName`, `FormatResourceType`,
`ToLpcwstr`, `FromLpcwstr`) matches the brief's reference code.

## Build + test commands and results (pristine)

All runs are inside the VS 18 BuildTools dev shell
(`Launch-VsDevShell.ps1 -Arch amd64`).

### minimal preset — build to verify failing test (before implementation)

```
cmake --build --preset minimal-MinSizeRel
```
Result: `FAILED ... fatal error C1083: Cannot open include file:
'core/resource_id.h': No such file or directory` (expected).

### minimal preset — build after implementation

```
cmake --build --preset minimal-MinSizeRel
```
Result: clean build, no warnings, `[6/7]` steps succeeded (`rcedit.exe` and
`rcedit_tests.exe` linked).

### minimal preset — ctest

```
ctest --preset minimal-MinSizeRel
```
```
Test project S:/llm/rcedit/build/minimal
    Start 1: version
1/5 Test #1: version ..........................   Passed    0.04 sec
    Start 2: error
2/5 Test #2: error ............................   Passed    0.02 sec
    Start 3: encoding
3/5 Test #3: encoding .........................   Passed    0.01 sec
    Start 4: resource_id
4/5 Test #4: resource_id ......................   Passed    0.01 sec
    Start 5: imports
5/5 Test #5: imports ..........................   Passed    0.05 sec

100% tests passed, 0 tests failed out of 5
```

### direct test binary — resource_id group detail

```
.\build\minimal\MinSizeRel\rcedit_tests.exe resource_id
```
```
[resource_id] ParsesHashNumeric
[resource_id] BareDecimalIsAString
[resource_id] RejectsBadHashForms
[resource_id] RejectsEmpty
[resource_id] TypeAliasesAreCaseInsensitiveWithOrWithoutPrefix
[resource_id] UnknownTypeIsAString
[resource_id] NameNeverUsesAliases
[resource_id] FormatsTypesWithAliasOrHash
[resource_id] FormatsNamesWithHashOrString
[resource_id] FormatRoundTrips
[resource_id] ParsesLang
[resource_id] Win32PointerConversion
[resource_id] KeyEquality
[resource_id] FormatterWorks
resource_id: 14 case(s), 0 failure(s)
```
All 14 cases pass, 0 failures.

### After clang-format -i — rebuild + retest (minimal preset)

Re-ran build and ctest after formatting to confirm no regressions:
build clean, `ctest --preset minimal-MinSizeRel` — 100% tests passed
(5/5), same as above.

### Cross-check: default preset (7z + zstd enabled)

Since `format.h` and the CMakeLists changes are shared across presets, also
verified the `default` preset (which links 7zip/zstd) still builds and
tests clean:

```
cmake --build --preset default-MinSizeRel
```
Clean build (13/13 steps, no warnings).

```
ctest --preset default-MinSizeRel
```
```
Test project S:/llm/rcedit/build/default
    Start 1: version
1/5 Test #1: version ..........................   Passed    0.04 sec
    Start 2: error
2/5 Test #2: error ............................   Passed    0.01 sec
    Start 3: encoding
3/5 Test #3: encoding .........................   Passed    0.01 sec
    Start 4: resource_id
4/5 Test #4: resource_id ......................   Passed    0.01 sec
    Start 5: imports
5/5 Test #5: imports ..........................   Passed    0.05 sec

100% tests passed, 0 tests failed out of 5
```

## clang-format

Ran `clang-format -i` on all created/touched files:
`src/core/resource_id.h`, `src/core/resource_id.cpp`, `src/core/format.h`,
`tests/test_resource_id.cpp`, `tests/main.cpp`. Rebuilt and re-ran tests
after formatting to confirm the reformatted code still compiles and passes
(see above) — repo's `.clang-format` (4-space indent, `AccessModifierOffset:
-4`, `SpacesInAngles: true`, etc.) reformatted the brief's reference style
into the codebase's established style; behavior unchanged.

## Concerns

None. All interfaces match the brief exactly (function names as specified
in "Accepted deviations": `ParseResourceType`/`ParseResourceName`/
`FormatResourceType`/`FormatResourceName`). Only the `ResourceId` formatter
was added, per the accepted deviation list. `EqualsIgnoreCase` from
`core/encoding.h` is consumed as required; no duplicate case-fold logic
remains in this task's code.
