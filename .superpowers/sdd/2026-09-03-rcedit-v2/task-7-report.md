# Task 7 report: Resource engine interface and Win32 implementation

Status: **BLOCKED** — one test (out of 13 in the `engine` group, out of 7 groups
total) fails due to real, empirically-verified Win32 `FindResourceExW`
language-fallback behavior that the brief's `Read()` implementation (given
verbatim) does not account for. Everything else is implemented exactly per
the brief and passes. Not committed (full suite is not green).

## What was implemented (Steps 1-4, verbatim per brief)

- `tests/fixture/fixture.c`, `tests/fixture/fixture.rc` — exact content from
  brief.
- `tests/temp.h` — `TempDir`, `CopyFixture` — exact content from brief.
- `tests/test_engine.cpp` — exact content from brief (13 test cases).
- `tests/CMakeLists.txt` — added `rcedit_fixture` executable +
  `add_dependencies`, added `temp.h`/`test_engine.cpp` to `rcedit_tests`
  sources, added `engine` to `RCEDIT_TEST_GROUPS`, changed the `foreach` to
  pass `$<TARGET_FILE:rcedit_fixture>` to every test group — all per brief.
  **One addition beyond the brief** (see "Deviation" below):
  `target_link_options(rcedit_fixture PRIVATE /MANIFEST:NO)`.
- `tests/main.cpp` — registered `RunEngineTests`, added `"engine"` to
  `kGroups` — per brief.
- `src/core/engine.h` — exact content from brief.
- `src/core/engine_win32.cpp` — exact content from brief.
- `src/core/CMakeLists.txt` — added `engine.h engine_win32.cpp` to
  `RCEDIT_CORE_SOURCES` — per brief.

## TDD evidence

### RED (before implementing engine.h/engine_win32.cpp)

Command:
```
cmake --preset minimal
cmake --build --preset minimal-MinSizeRel
```
Failing output (expected per brief Step 3):
```
[17/21] Building CXX object tests\CMakeFiles\rcedit_tests.dir\MinSizeRel\test_engine.cpp.obj
FAILED: [code=2] tests/CMakeFiles/rcedit_tests.dir/MinSizeRel/test_engine.cpp.obj
...
S:\llm\rcedit\tests\test_engine.cpp(11): fatal error C1083: Cannot open include file: 'core/engine.h': No such file or directory
```
This matches the brief's Step 3 exactly.

### GREEN attempt (after implementing engine.h/engine_win32.cpp)

Command:
```
cmake --preset minimal
cmake --build --preset minimal-MinSizeRel
ctest --preset minimal-MinSizeRel --output-on-failure
```
Build: clean, no warnings (project builds with `/W4 /WX`).

Test result:
```
1/7 Test #1: version ..........................   Passed
2/7 Test #2: error ............................   Passed
3/7 Test #3: encoding .........................   Passed
4/7 Test #4: resource_id ......................   Passed
5/7 Test #5: codec ............................   Passed
6/7 Test #6: engine ...........................***Failed    0.34 sec
  FAIL S:\llm\rcedit\tests\test_engine.cpp:82: engine->Read(wrongLang, data) == errc::resource_not_found
engine: 13 case(s), 1 failure(s)
7/7 Test #7: imports ..........................   Passed

86% tests passed, 1 tests failed out of 7
```

12/13 `engine` cases pass. The single failure is
`ReadMissingIsNotFound`'s second assertion (line 82):

```cpp
const ResourceKey wrongLang{kBaseline.type, kBaseline.name, 1033};
CHECK(engine->Read(wrongLang, data) == errc::resource_not_found);
```

## Root cause (confirmed independent of rcedit code)

I wrote three standalone Win32 probes (in scratchpad, not part of the repo)
that call `LoadLibraryExW`/`FindResourceExW` directly against
`rcedit_fixture.exe` — bypassing rcedit's engine entirely — to rule out a bug
in my transcription:

1. Enumeration confirms the fixture's only resource is exactly
   `type=#10, name=FIXTURE, lang=0` (one entry, as intended — see "Deviation"
   below for how that was achieved).
2. `FindResourceExW(module, MAKEINTRESOURCEW(10), L"FIXTURE", 1033)` —
   **succeeds**, returning the size-7 resource, even though it is stored only
   under language 0.
3. Repeating with `lang = 2052`, `1036`, and `0xFFFF` (nonsense) — **all
   succeed** the same way.

This is documented Win32 behavior: when `FindResourceExW` can't find an exact
language match for a given `(type, name)`, it falls back through a search
order (calling thread's language, system language, English, then simply "the
first language present for that resource") rather than failing outright. When
a resource has only one language variant, `FindResourceExW` finds it for
*any* requested language — `ERROR_RESOURCE_LANG_NOT_FOUND` is never raised.

This directly contradicts the brief's "Engine rules" statement that "`Read`
maps 1813/1814/1815 to `errc::resource_not_found`" for a wrong-language
lookup, and the `Read()` implementation given verbatim in the brief (a single
`FindResourceExW(..., *key.lang)` call with no further check) cannot produce
`errc::resource_not_found` for this scenario — not because of a
transcription error, but because the OS call it relies on doesn't fail here.

Making this test pass would require a design decision not present in the
brief (e.g. cross-checking the requested language against
`EnumResourceLanguagesW` before/after the `FindResourceExW` call, or
comparing the returned `HRSRC` against the one found for the exact language).
Per my instructions ("Do not invent alternative designs" / "STOP ... don't
guess" on unanticipated brief/build issues), I did not make that call myself.

## Deviation from the brief (mechanical, not a design decision)

`tests/CMakeLists.txt`'s `add_executable(rcedit_fixture ...)` as given
verbatim in the brief produces an executable that MSVC's linker
auto-embeds a default manifest resource into (`RT_MANIFEST` #24, name
`CREATEPROCESS_MANIFEST_RESOURCE_ID` #1, lang 1033), which broke the
"exactly one resource" fixture baseline the brief itself declares
(`EnumerateBaseline`/several other tests assert `entries.size() == 1`).
Confirmed via a standalone `EnumResourceTypesW` probe before and after the
fix. I added:
```cmake
target_link_options(rcedit_fixture PRIVATE /MANIFEST:NO)
```
right after the `add_executable(rcedit_fixture ...)` line. This is scoped
only to the test fixture target (does not touch `rcedit`'s own linker flags
or the import-allowlist-checked executable) and is the standard, well-known
way to suppress MSVC's automatic manifest resource. I judged this to be
mechanical build-config correction needed to realize the brief's own stated
fixture contract, not a design choice about the engine, so I applied it
rather than stopping — flagging it here for visibility regardless.

## Self-review of the diff

- `engine.h` / `engine_win32.cpp`: transcribed verbatim from the brief,
  character-for-character except for whitespace later normalized by
  `clang-format` (not yet run — see below, since I stopped before Step 6).
- `test_engine.cpp`, `temp.h`, `fixture/fixture.c`, `fixture/fixture.rc`:
  transcribed verbatim from the brief.
- `tests/CMakeLists.txt`, `tests/main.cpp`, `src/core/CMakeLists.txt`:
  changes match the brief's described edits exactly, plus the one
  `/MANIFEST:NO` addition documented above.
- No other files touched. `.superpowers/` was not staged or modified by me
  (the working tree shows `progress.md` under `.superpowers/sdd/...` as
  modified, but that predates my work in this task and I did not edit it or
  stage it).
- `clang-format -i` (Step 6) was **not** run yet, and nothing was committed,
  because the full suite is not green and the brief instructs the wrong-path
  troubleshooting hints only for the two failure modes it anticipated
  (language-not-0-in-fixture, sharing violation at `BeginUpdateResourceW`) —
  neither of which is what's occurring here.

## Files changed (uncommitted, on disk)

- `S:\llm\rcedit\src\core\engine.h` (new)
- `S:\llm\rcedit\src\core\engine_win32.cpp` (new)
- `S:\llm\rcedit\src\core\CMakeLists.txt` (modified)
- `S:\llm\rcedit\tests\fixture\fixture.c` (new)
- `S:\llm\rcedit\tests\fixture\fixture.rc` (new)
- `S:\llm\rcedit\tests\temp.h` (new)
- `S:\llm\rcedit\tests\test_engine.cpp` (new)
- `S:\llm\rcedit\tests\CMakeLists.txt` (modified)
- `S:\llm\rcedit\tests\main.cpp` (modified)

## What I need from the controller

A ruling on how `Read()` should behave for a language that isn't present
when other languages for the same `(type, name)` are (or, as here, when it's
the only language and the request is for a different one), given that
`FindResourceExW`'s built-in fallback makes the brief's literal
implementation unable to distinguish "found under fallback" from "found
under an exact match." Options I see (not choosing among them myself):

1. Change `Read()` to verify the exact language via
   `EnumResourceLanguagesW` (or compare against a fresh `FindResourceExW`
   with each enumerated language) before accepting the result, only treating
   an *exact* match as success and otherwise returning
   `errc::resource_not_found`.
2. Accept the OS fallback semantics as intended behavior and adjust the test
   (either remove the `wrongLang` sub-case or change its expectation to
   success with the baseline's data, since that is what a real caller would
   observe).
3. Something else the controller prefers.

I did not implement any of these since they are design decisions outside
"careful transcription" of the brief.

---

## Fix applied per Ruling R19 / R20

**R19 accepted and applied verbatim (transcription, no redesign).** Added to
`src/core/engine_win32.cpp`:

- `LangMatchContext` struct + `OnLanguageMatch` callback in the anonymous
  namespace, placed right after `OnType` (next to the other
  `OnType`/`OnName`/`OnLanguage` enumeration callbacks).
- `Win32Engine::RequireExactLanguage(const ResourceKey&)` private member,
  placed immediately before `BeginUpdate` (both are now grouped under
  `private:`).
- `Read()` now calls `RequireExactLanguage(key)` right after the
  `if (!key.lang)` guard and before the existing `FindResourceExW` call,
  returning `errc::resource_not_found` when the exact language isn't present
  for that `(type, name)`. The rest of `Read()` — `FindResourceExW`,
  `LoadResource`, `SizeofResource`, `LockResource`, and the existing
  `IsNotFound` branch — is unchanged, left in place as defense per the
  ruling.

Applied exactly as given in the ruling, character-for-character (before
`clang-format`).

**R20 confirmed kept**: `target_link_options(rcedit_fixture PRIVATE
/MANIFEST:NO)` in `tests/CMakeLists.txt` is unchanged from the original
implementation — ruled an accepted deviation.

### GREEN (after R19)

Command:
```
cmake --build --preset minimal-MinSizeRel
ctest --preset minimal-MinSizeRel --output-on-failure
```
Output:
```
1/7 Test #1: version ..........................   Passed
2/7 Test #2: error ............................   Passed
3/7 Test #3: encoding .........................   Passed
4/7 Test #4: resource_id ......................   Passed
5/7 Test #5: codec ............................   Passed
6/7 Test #6: engine ...........................   Passed
7/7 Test #7: imports ..........................   Passed

100% tests passed, 0 tests failed out of 7
```

Verbose run of the `engine` group directly (`rcedit_tests.exe engine
<fixture-path>`) confirms all 13 cases individually:
```
[engine] OpenMissingFileFails
[engine] EnumerateBaseline
[engine] ReadBaseline
[engine] ReadMissingIsNotFound
[engine] WriteReadOnlyIsRejected
[engine] WriteWithoutLangIsRejected
[engine] WriteEmptyIsRejected
[engine] WriteCommitReadBack
[engine] OverwriteReplacesContent
[engine] RemoveCommit
[engine] ReadAfterWriteIsNotPermitted
[engine] DiscardLeavesFileUnchanged
[engine] DestructorWithoutCommitDiscards
engine: 13 case(s), 0 failure(s)
```
(The `[W] Resource update session ... was not committed, discarding` line is
expected stderr output from `DestructorWithoutCommitDiscards`'s destructor
warning, not a failure.)

Re-verified GREEN again after running `clang-format -i` on the touched
files (Step 6) — clean rebuild, same 100% pass result.

## Final status: DONE

Committed with identity `fabienfl <fabien.fl-orc@ssi.gouv.fr>`. No
`.superpowers/` paths staged (verified via `git show --stat`).
