# Final review fixes - report

Commit: `648d315` on `master` (parent `6b10d24`)
Subject: `Final review fixes: guard callback boundaries, tidy output-on-failure, CI hardening`

## Fix 1 - guard OS/COM callbacks (no exception across a boundary)

- `src/core/engine_win32.cpp`
  - `OnLanguage` (~line 47-72), `OnName` (~line 74-95), `OnType` (~line 97-119):
    each body wrapped in `try { ... } catch (...) { ... return FALSE; }`.
  - `EnumContext` lost its `HMODULE module` field (see Fix 5) but kept `ec`.
    Each catch block sets `ctx->ec = std::make_error_code(std::errc::not_enough_memory);`
    then `return FALSE`.
  - **Error surfacing**: `Enumerate()` (the caller of `EnumResourceTypesW`)
    already had the logic to check `ctx.ec` first, ahead of interpreting
    `GetLastError()`/`IsEnumerationEnd()`, at two points: right after
    `EnumResourceTypesW` returns FALSE, and again unconditionally right
    after that block (`if (ctx.ec) { return ctx.ec; }` before
    `entries = std::move(found)`). Because `OnType`/`OnName`/`OnLanguage`
    already return `FALSE` on any local Win32 failure and propagate that
    FALSE up through the whole callback chain (each level's own
    `!EnumResourceXxxW(...)` check + `ctx->ec ? FALSE : TRUE`), a
    `ctx->ec` set inside the innermost `OnLanguage`'s catch block
    propagates the same way an existing `LastWin32Error()` failure already
    did: `OnLanguage` returns FALSE -> `EnumResourceLanguagesW` in `OnName`
    fails -> `OnName`'s `IsEnumerationEnd` check does NOT clear `ctx->ec`
    (it's non-zero, `IsEnumerationEnd` is only consulted when re-deriving
    from `GetLastError()`, and the final `return ctx->ec ? FALSE : TRUE;`
    line short-circuits straight to FALSE) -> same chain up through
    `OnType` -> `EnumResourceTypesW` fails at the top -> `Enumerate()` sees
    `ctx.ec` set and returns it. No new logic was needed in `Enumerate()`;
    it already deferred to `ctx.ec` before consulting `GetLastError()`,
    which is exactly the mechanism a caught bad_alloc now uses. Net effect:
    a `bad_alloc` during enumeration becomes an `errc` equivalent to
    `std::errc::not_enough_memory`, not a truncated/partial `entries` list
    reported as success.
- `src/core/sevenzip/extract_callback.cpp`
  - `ExtractCallback::GetStream` (~line 29-53): `m_output.clear()` +
    `new OutMemStream(...)` + `stream.Detach()` wrapped in
    `try { ... } catch (...) { return E_OUTOFMEMORY; }`, matching
    `OutMemStream::Write`/`SetSize`'s existing convention.
- `src/core/sevenzip/update_callback.cpp`
  - `UpdateCallback::GetStream` (~line 91-114): `new InMemStream(...)` +
    `stream.Detach()` wrapped the same way, returns `E_OUTOFMEMORY`.
  - `UpdateCallback::GetProperty` (~line 52-93): the whole
    `CPropVariant`/`switch`/`Detach` body wrapped the same way (covers
    `prop = m_name.c_str()` and every other allocating assignment plus
    `prop.Detach(value)`), returns `E_OUTOFMEMORY`.
- `OutMemStream::Write`/`SetSize` were left untouched (already guarded).

## Fix 2 - no stray --output file on a failed op

- `src/core/ops.cpp`: added `RemoveStrayOutput(output)` (anonymous
  namespace, right after `CopyToOutput`, ~line 104-115): a no-op when
  `output` is `std::nullopt`, otherwise `std::filesystem::remove(*output, ec)`
  best-effort (its own `ec` is discarded). `pe` is never touched.
  Called at every failure return in `Set()` (Open/Write/Commit, ~line
  277-290) and `Remove()` (Open/ResolveLanguage/Remove/Commit, ~line
  307-327) that occurs after `CopyToOutput` has already run. R21 ordering
  (compress before CopyToOutput in `Set`) is untouched; failures before
  `CopyToOutput` runs still return directly with no cleanup needed.

- **Correctness issue found and fixed while testing**: `Win32Engine::Open()`
  maps the module with `LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE`, which opens the
  file with no sharing (no `FILE_SHARE_DELETE`). `Write()`/`Remove()`
  release that mapping as their first action (inside `BeginUpdate()`,
  `m_module.reset()`, unconditionally once the mode/lang precondition
  checks pass) before ever touching the update session, so by the time
  those calls return (success or failure) the lock is already gone and
  `RemoveStrayOutput` succeeds. But `ResolveLanguage()` (called between
  `Open()` and `Remove()` inside `ops::Remove`) only calls `Enumerate()`
  read-only and never touches `m_module`; when it fails (e.g. the
  resource does not exist), `engine.Discard()` used to be a no-op if no
  update session had started, leaving the exclusive mapping open and
  making the subsequent `fs::remove(*output)` fail with a sharing
  violation (silently, since the removal is best-effort) -- the exact
  bug Fix 2 is meant to close. Fixed by making
  `Win32Engine::Discard()` (`src/core/engine_win32.cpp`, ~line 319-338)
  unconditionally call `m_module.reset()` first, before its existing
  `m_update`/`EndUpdateResourceW` logic, so "discard this session" always
  releases any handle Open() is holding, not only an in-progress update.
  This is a natural extension of Discard's documented role ("...must be
  reopened" after Discard) and does not change behavior for any existing
  caller: in every prior call site (`Set`/`Remove`'s Write/Remove-failure
  branches, the destructor's un-committed-session path, and the existing
  `test_engine.cpp` Discard tests) `m_module` was already null by the time
  `Discard()` ran, because `BeginUpdate()` had already reset it.

- **Test added**: `tests/test_ops.cpp` gained
  `SetFailureAfterCopyRemovesOutput` (writes a non-PE file as the source so
  `engine.Open` deterministically fails on the post-copy `target`, forcing
  the Open-failure branch in `Set`) and
  `RemoveFailureAfterCopyRemovesOutput` (uses a `CONFIG` key that does not
  exist in the fixture, deterministically failing `ResolveLanguage` inside
  `Remove` after `CopyToOutput` already ran). Both assert
  `!std::filesystem::exists(out)` after the call returns an error. Both
  are fully deterministic (no filesystem locking/timing tricks), registered
  in `kCases`, and exercised across every preset in this run. A
  Set-path failure inside `Write`/`Commit` specifically (as opposed to
  `Open`) was not additionally tested, since forcing `UpdateResourceW`/
  `EndUpdateResourceW` itself to fail deterministically from the test
  harness would need real OS-level tricks (e.g. holding a conflicting
  handle) that are themselves flaky across CI runners; the `Open`-failure
  test already exercises the same `RemoveStrayOutput` call site, and the
  reasoning above (`BeginUpdate` always releases `m_module` before
  `UpdateResourceW`/`EndUpdateResourceW` run) covers why those other
  branches are safe.

## Fix 3 - ERROR_RESOURCE_DATA_NOT_FOUND (1812) in RequireExactLanguage

- Checked `IsNotFound`'s three call sites (`IsEnumerationEnd`,
  `Win32Engine::Read`, `Win32Engine::RequireExactLanguage`) before
  deciding: extending `IsNotFound()` globally is low-risk (`IsEnumerationEnd`
  already ORs 1812 in separately, so it would become a harmless
  duplicate; `Read` would additionally start treating a 1812 from
  `FindResourceExW` as `resource_not_found` instead of a raw Win32 error,
  which is arguably an improvement but is an untested/unrequested behavior
  change for that call site). Went with the task's explicitly safer option
  instead: extended the condition locally, only in
  `RequireExactLanguage`'s fallback branch (`src/core/engine_win32.cpp`,
  ~line 356-362):
  `if( IsNotFound( code ) || code == ERROR_RESOURCE_DATA_NOT_FOUND )`.
  `IsNotFound()` itself and its other two call sites are unchanged.

## Fix 4 - CI hardening

- `.github/workflows/build.yml`: added a top-level
  `permissions:\n  contents: read` block (after `on:`, before `jobs:`) and
  `timeout-minutes: 60` on the `build` job (right after `runs-on:`).
  Nothing else in the workflow was touched.

## Fix 5 - cost-free cleanups

- `src/core/engine_win32.cpp`: removed `EnumContext::module` (grepped the
  whole file for `.module`/`->module` first -- the only other reference was
  its own assignment `ctx.module = m_module.get();` in `Enumerate()`,
  which was also removed; the three callbacks use their own `module`
  parameter, never `ctx->module`).
- `tests/CMakeLists.txt` (~line 58-60): reworded the manifest comment to
  state type and name explicitly and accurately: "resource type
  RT_MANIFEST = 24, name CREATEPROCESS_MANIFEST_RESOURCE_ID = 1, lang
  1033".
- `src/cli/CMakeLists.txt` (~line 18-25): reworded the "no default
  libraries" comment to note that `/NODEFAULTLIB` only drops libs pulled
  in via `#pragma comment(lib, ...)`, that CMake's implicit
  `CMAKE_CXX_STANDARD_LIBRARIES` (gdi32.lib, shell32.lib, ole32.lib,
  user32.lib, etc.) still lands on the link line regardless, and that the
  actual guarantee is the `imports` CTest against the real PE import
  table (`check_imports.cmake`), not the explicit lib list by itself.

## Explicitly not touched (out of scope, per instructions)

- `List`'s behavior on an unreadable resource: unchanged.
- `CMAKE_CONFIGURATION_TYPES ... CACHE ... FORCE` handling: unchanged.
- No ambiguous-language CLI candidate-listing test added.

## Build / test results

All commands run in the VS amd64 dev shell
(`Launch-VsDevShell.ps1 -Arch amd64`), sequentially, reading each
result before moving to the next.

Run 1 (before the Discard() fix) surfaced the bug described in Fix 2 above:
`no-7z-MinSizeRel`'s `ops` CTest failed at
`tests/test_ops.cpp:214: !std::filesystem::exists( out )`
(`RemoveFailureAfterCopyRemovesOutput`). Fixed by extending
`Win32Engine::Discard()` as described, then reran everything from a clean
configure through test:

| Preset               | Configure | Build | CTest                |
|-----------------------|-----------|-------|----------------------|
| minimal-MinSizeRel    | OK        | OK    | 11/11 passed (100%)  |
| no-7z-MinSizeRel      | OK        | OK    | 11/11 passed (100%)  |
| no-zstd-MinSizeRel    | OK        | OK    | 12/12 passed (100%)  |
| default-MinSizeRel    | OK        | OK    | 13/13 passed (100%)  |
| default-Debug         | n/a       | OK    | 13/13 passed (100%)  |

- `imports` CTest passed in every preset run (5/5): KERNEL32-only for
  minimal/no-7z/no-zstd, +OLEAUT32+USER32 for default (7z enabled) --
  matrix unchanged.
- No compiler warnings anywhere in the full rebuild log (`grep -ic
  warning` on the captured output = 0), consistent with `/W4 /WX` staying
  clean.
- `cli_smoke` / `cli_smoke_7z` passed wherever present.

## Concerns

- The `Win32Engine::Discard()` change (releasing `m_module` unconditionally,
  not just when an update session is active) is slightly broader than a
  purely local one-line fix, but it was necessary for Fix 2 to actually
  work on the `Remove()`-before-`BeginUpdate()` failure path
  (`ResolveLanguage` failing after `CopyToOutput`), which is a very
  plausible real-world case
  (`rcedit remove --output out.exe app.exe TYPE:MISSING_NAME`). Verified it
  does not change behavior for any existing caller (every pre-existing
  `Discard()` call site already had `m_module == nullptr` by the time it
  ran).
- No test was added specifically for a `Write`/`Commit`-stage failure in
  `Set()` after `CopyToOutput` (only the `Open`-stage failure is tested
  there); the reasoning why that is safe is documented above and in the
  test's surrounding comment in `tests/test_ops.cpp`.
