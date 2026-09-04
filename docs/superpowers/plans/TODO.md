# rcedit v2: handoff TODO

Read this first after a context reset. Everything below is the state as of
2026-09-03, commit `30e1d5f`.

## Where things are

- Spec (approved): `docs/superpowers/specs/2026-09-03-rcedit-v2-design.md`
- Implementation plan (11 tasks, TDD, full code): `docs/superpowers/plans/2026-09-03-rcedit-v2.md`
- Old rcedit v1 tree still present in `src/` and `CMakeLists.txt`. Task 1 of the plan deletes it.
- Reference projects, read-only: `S:\llm\dfir-orc-forge` (7zip overlay port, presets, CI) and `S:\llm\orc\dfir-orc\src\OrcCapsule` (zstd helpers, CLI style).

## Next step

Execute the plan task by task. The user has not yet chosen between:

1. `superpowers:subagent-driven-development` (fresh subagent per task, review between tasks), recommended.
2. `superpowers:executing-plans` (inline in this session, batched with checkpoints).

Ask which one, then start at Task 1. Do not skip the failing-test steps.

## Environment facts you will need

- Run every `cmake` from a Visual Studio 2022 x64 developer shell (Ninja and `cl` on PATH). PowerShell: `& "C:\Program Files\Microsoft Visual Studio\2022\<Edition>\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64`.
- Git identity is not configured on this machine. Commit with `git -c user.name=fabienfl -c user.email=fabien.fl-orc@ssi.gouv.fr commit ...`. Do not write it to config.
- The repo is on a network share; `safe.directory` for `S:/llm/rcedit` was already added to the global git config.
- The Bash tool rejects very long commands (`ENAMETOOLONG`). Write big files with the Write tool, not heredocs.
- vcpkg builds 7-Zip and zstd from source on the first `default` configure; expect several minutes.

## Decisions already made (do not re-open)

- Static only: static CRT, static 7-Zip and zstd, triplet `x64-windows-static`, no dynamic-runtime preset.
- Import allowlist: `KERNEL32.dll` always; `OLEAUT32.dll` only when `RCEDIT_ENABLE_7Z` (7-Zip uses `SysAllocString`; OLEAUT32 is a KnownDLL, not side-loadable). Any other DLL fails the `imports` CTest. If the linker wants a symbol from another DLL, stop and report the symbol names; do not add libraries. `uuid.lib` and, if needed, `comsuppw.lib` are static libs and allowed with 7z.
- Commands: `list`, `get`, `set`, `remove`, `hexdump`. In-place edit by default, `--output` copies first. `get`/`hexdump` decompress by magic unless `--raw`; a detected but compiled-out codec is an error.
- Identifiers: `RT_RCDATA` / `RCDATA` / `#10` / string for types, `#n` or string for names (bare number is a string), `--lang` decimal or `0x` hex. Missing `--lang` on get/remove/hexdump: neutral, else the only language, else `ambiguous_language` listing candidates.
- Windows only for now; `wmain`, wide strings. Engine interface `ResourceEngine` isolates Win32 for a future portable PE engine.
- Tests are hand-rolled (`tests/check.h`), one CTest per group, plus `imports` and `cli_smoke` CTests. Fixture PE has exactly one resource: `RT_RCDATA` / `FIXTURE` / lang 0 / bytes `fixture`.
- Deviations from the spec, on purpose (see plan self-review): enum is `rcedit::errc`; command `validate` returns `std::optional<std::wstring>`; `Hexdump` returns `std::wstring`; `Out::Write(wstring_view)` plus `Out::Print(fmt, ...)`.

## Open items after the plan is done

The two future-work items below now live in `docs/BACKLOG.md`, with the rest of
the backlog. Look there, not here.

- Push to GitHub to validate `.github/workflows/build.yml` (cannot be verified locally).
- Future work noted in the spec, not to implement now: portable PE engine (Linux), manifest editing, JSON output for `list`.
