# Task 11 report: CI workflow and README

## Files created

- `.github/workflows/build.yml`
- `README.md`

Both transcribed from `task-11-brief.md` with the four corrections applied.

## Corrections applied

1. **R3 — no CMakeLists edit.** Verified before touching anything: `/Zi` is
   already at `CMakeLists.txt:64` (in the `add_compile_options(/W4 /WX
   /guard:cf /EHsc /sdl /utf-8 /external:W0 /Zi)` line) and `/DEBUG /OPT:REF
   /OPT:ICF` is already on the `rcedit` target's `target_link_options` at
   `src/cli/CMakeLists.txt:19`. `build/default/MinSizeRel/rcedit.pdb` already
   existed before any work started. No CMakeLists.txt file was modified in
   this task.

2. **R16 — tag-gated artifact upload.** The `Upload artifact` step's `if:`
   condition was changed from the brief's `matrix.preset == 'default'` to:
   ```yaml
   if: matrix.preset == 'default' && startsWith(github.ref, 'refs/tags/')
   ```
   Everything else in the workflow (runner `windows-2025`, the
   `vcvars64.bat` path for VS 2022 Enterprise, the push/tag/PR triggers,
   the matrix, the cache step, configure/build/test steps) was transcribed
   unchanged from the brief.

3. **R17 — accurate preset statement.** Replaced the brief's inaccurate
   "Each has `-Debug`, `-RelWithDebInfo` (default only) and `-MinSizeRel`
   build and test presets." with:
   > The `default` preset has `-Debug`, `-RelWithDebInfo` and `-MinSizeRel`
   > build and test presets; `no-7z`, `no-zstd` and `minimal` have
   > `-MinSizeRel` only.

   Verified directly against `CMakePresets.json`: `default` has
   `default-Debug`, `default-RelWithDebInfo`, `default-MinSizeRel` build and
   test presets; `no-7z`, `no-zstd`, `minimal` each have only a
   `-MinSizeRel` build and test preset. The statement matches exactly. The
   following sentences about `RCEDIT_ENABLE_7Z`/`RCEDIT_ENABLE_ZSTD`/
   `RCEDIT_BUILD_TESTS` and the vcpkg submodule/overlay port were kept as in
   the brief.

4. **R24 — USER32.dll in the import allowlist sentence.** Changed the
   brief's "imports nothing but `KERNEL32.dll` (plus `OLEAUT32.dll`, a
   KnownDLL, when the 7z codec is built in)" to:
   > imports nothing but `KERNEL32.dll` (plus `OLEAUT32.dll` and
   > `USER32.dll`, both KnownDLLs, when the 7z codec is built in).

## Local verification

Ran in the VS 2022 dev shell (`Launch-VsDevShell.ps1 -Arch amd64`):

```
cmake --build --preset default-MinSizeRel   -> "ninja: no work to do" (already up to date; no code changed this task)
Test-Path build/default/MinSizeRel/rcedit.pdb   -> True
ctest --preset default-MinSizeRel               -> 100% tests passed, 0 tests failed out of 13 (2.87 sec)
```

All 13 tests passed: version, error, encoding, resource_id, codec, engine,
ops, args, commands, sevenzip, imports, cli_smoke, cli_smoke_7z.

Also spot-checked that `docs/superpowers/specs/2026-09-03-rcedit-v2-design.md`
(referenced by the README's Design section) exists.

## Workflow verification note

The `.github/workflows/build.yml` file itself was **not** and cannot be
executed locally — `windows-2025` and the `vcvars64.bat` path under
`C:\Program Files\Microsoft Visual Studio\2022\Enterprise\...` target
GitHub's hosted runner image (VS 2022 Enterprise), which does not match this
local machine's toolchain layout (VS Build Tools 18 under
`C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`). Per the
task instructions, these paths and the runner label were deliberately left
unchanged from the brief since they target GitHub's environment, not this
machine. The workflow's correctness (trigger matching, matrix, caching,
step ordering, artifact gating) can only be confirmed by pushing to GitHub
and observing an Actions run.

## Self-review

- Diffed the transcribed workflow and README against the brief line by
  line; the only differences are the four corrections listed above.
- Confirmed via `git show --stat HEAD` that the commit contains exactly
  `.github/workflows/build.yml` and `README.md`, two files, 148 insertions,
  0 deletions — no `.superpowers/` paths and no CMakeLists.txt changes.
- Pre-existing unstaged changes in `.superpowers/sdd/.gitignore` and
  `.superpowers/sdd/2026-09-03-rcedit-v2/progress.md` (not made by this
  task) were left untouched and were not staged or committed, per the
  "never git add anything under `.superpowers/`" instruction.
- Did not use `git add -A` (as the brief's Step 3 literally suggested);
  used explicit `git add .github/workflows/build.yml README.md` instead to
  guarantee `.superpowers/` was excluded.
- Verified the R17 preset claim against the actual `CMakePresets.json`
  content rather than trusting the brief's correction text at face value.

## Concerns

None. Task is complete and self-contained: two new files, no code changes,
local build/tests green, commit clean.
