# Task 9 report: CLI argument parser

## Status: DONE

## What was implemented

- `src/cli/args.h` / `src/cli/args.cpp` (namespace `rcedit::cli`): `OptionSpec`,
  `CommandSpec`, `ParsedArgs` (with `Has`/`Value`), `UsageError`, `Parse`,
  `FormatUsage`, `FormatCommandUsage` — transcribed from the task-9 brief,
  with Ruling R11 applied (see below).
- `tests/test_args.cpp` — transcribed verbatim from the brief (12 cases).
- Wiring: `src/cli/CMakeLists.txt` adds `args.h`/`args.cpp` to the `rcedit`
  target; `tests/CMakeLists.txt` adds `test_args.cpp` and
  `${CMAKE_SOURCE_DIR}/src/cli/args.cpp` to `rcedit_tests` sources and `args`
  to `RCEDIT_TEST_GROUPS`; `tests/main.cpp` declares and registers
  `RunArgsTests` in `kGroups`, replacing the "Later tasks add" placeholder
  comment.

## Ruling R11 applied

Per the task instructions, the brief's `FormatUsage` global-options loop
(inline shortName-ternary label + hardcoded `{:<18}`) was replaced with the
shared, content-computed-width approach:

- Added file-local `FlagLabel(wchar_t shortName, std::wstring_view longName)`
  in the anonymous namespace of `args.cpp`.
- `OptionLabel(const OptionSpec&)` now builds its label via `FlagLabel(...)`
  instead of duplicating the ternary (identical output to the brief's
  version).
- `FormatUsage`'s global-options section now computes `globalWidth` as
  `max` over `FlagLabel(global.shortName, global.longName).size()` across
  `kGlobals`, and formats each row as
  `L"  {:<{}}  {}\n"` with that computed width — mirroring the
  `FormatCommandUsage` command-options style (2-space gap), no magic `18`.

This is the only deviation from the brief's literal text, and it is exactly
the ruling's prescribed change. All other code (`args.h`, `Parse`'s token
handling and ordered error precedence, `FormatUsage`'s commands section,
`FormatCommandUsage`) was transcribed as given.

## TDD evidence

### RED

Wrote `tests/test_args.cpp` and wired it into `tests/CMakeLists.txt` /
`tests/main.cpp` *before* creating `src/cli/args.h`/`args.cpp` or updating
`src/cli/CMakeLists.txt` to reference them.

Command:
```
cmake --build --preset minimal-MinSizeRel
```
Failure (configure-time, since `tests/CMakeLists.txt` already referenced the
not-yet-created source file):
```
CMake Error at external/vcpkg/scripts/buildsystems/vcpkg.cmake:615 (_add_executable):
  Cannot find source file:
    S:/llm/rcedit/src/cli/args.cpp
Call Stack (most recent call first):
  tests/CMakeLists.txt:8 (add_executable)
CMake Error at external/vcpkg/scripts/buildsystems/vcpkg.cmake:615 (_add_executable):
  No SOURCES given to target: rcedit_tests
```
This is the RED state anticipated by the brief ("cli/args.h not found") —
here it surfaces one step earlier, as a missing-source-file configure error,
because `args.cpp` (which doesn't exist yet) was referenced directly in
`tests/CMakeLists.txt` rather than `#include`d. Confirms the test harness is
wired up and fails for the expected reason (implementation absent).

### GREEN

Implemented `src/cli/args.h`, `src/cli/args.cpp` (with R11 applied) and
`src/cli/CMakeLists.txt`.

Command:
```
cmake --build --preset minimal-MinSizeRel
```
Result: builds cleanly (`rcedit.exe` and `rcedit_tests.exe` link with no
warnings/errors).

Focused group:
```
build\minimal\MinSizeRel\rcedit_tests.exe args build\minimal\MinSizeRel\rcedit_fixture.exe
```
Output:
```
[args] ParsesCommandPathAndOptions
[args] OptionsMayPrecedeThePath
[args] GlobalsAnywhere
[args] HelpAndVersionShortCircuit
[args] MissingCommandOrPath
[args] ExtraPositionalIsAnError
[args] UnknownDuplicateAndMissingValue
[args] RequiredOptionEnforced
[args] ValuesMayStartWithDash
[args] DoubleDashEndsOptions
[args] ValidateIsCalled
[args] UsageMentionsEverything
args: 12 case(s), 0 failure(s)
```

Full suite:
```
ctest --preset minimal-MinSizeRel
```
```
100% tests passed, 0 tests failed out of 9
```
(version, error, encoding, resource_id, codec, engine, ops, args, imports —
all Passed; `imports` confirms the new CLI code adds no new DLL imports
beyond the existing allowlist.)

Re-ran build + full ctest suite again after `clang-format -i` to confirm
formatting didn't change behavior — still 9/9 passed.

## Files changed

- `src/cli/args.h` (new)
- `src/cli/args.cpp` (new)
- `src/cli/CMakeLists.txt` (modified: added `args.h`, `args.cpp` to `rcedit`
  sources)
- `tests/test_args.cpp` (new)
- `tests/CMakeLists.txt` (modified: added `test_args.cpp` and
  `${CMAKE_SOURCE_DIR}/src/cli/args.cpp` to `rcedit_tests`; added `args` to
  `RCEDIT_TEST_GROUPS`)
- `tests/main.cpp` (modified: declared `RunArgsTests`, registered it in
  `kGroups`, removed the "Later tasks add: RunArgsTests." placeholder
  comment)

## Self-review findings

- `clang-format -i` reformatted all four touched/new source files (`args.h`,
  `args.cpp`, `test_args.cpp`, `main.cpp`) from the brief's compact style to
  the project's spaced-brace, 4-space-indent, wrap-at-column style already
  used throughout `tests/check.h`, `tests/test_version.cpp`, etc. This is
  expected and cosmetic only — rebuilt and re-ran the full suite afterward
  to confirm no behavioral change (still 9/9 passed).
- Verified `git show --stat HEAD` lists exactly the 6 intended files with no
  `.superpowers/` paths; those scratch files (`.gitignore`,
  `progress.md`) were left unstaged and uncommitted, as required.
- Confirmed `Parse`'s error-precedence order matches the brief exactly:
  help/version short-circuit -> missing command -> missing PE path ->
  verbose+quiet -> duplicate option / unknown option / missing value (all
  detected inline during the token loop, in encounter order) -> missing
  required option -> `validate`. This matches all 12 test cases' exact
  expectations (e.g. `RequiredOptionEnforced` triggers only after path/
  verbose+quiet checks pass; `ValidateIsCalled` triggers only after required
  options are satisfied).
- No cross-task coupling beyond `Version()` via `AnsiToUtf16(Version())` in
  `FormatUsage`, as scoped. Task 10 (wiring specs to Task 8 operations) is
  untouched.

## Concerns

None. Build and full test suite are green; the only prescribed change (R11)
was applied exactly as specified; no ambiguity encountered.
