# SDD ledger — plan: docs/superpowers/plans/2026-09-03-rcedit-v2.md

Setup: 2026-09-03. Spec read: docs/superpowers/specs/2026-09-03-rcedit-v2-design.md (authority). Plan: 11 tasks.
Ruling: work directly on master (no worktree/branch) — user explicitly chose "Work on master" when asked — cost if wrong: rewrite commits land on master and must be reverted/rebased rather than dropped with a branch.

## Environment (corrected from TODO.md)
- VS dev shell: `& "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64`  (TODO's `2022\<Edition>` path is WRONG on this machine — only VS 18 BuildTools is installed).
- Toolchain: MSVC 14.51.36231 (C++23 capable), bundled CMake 4.3.1-msvc1, bundled Ninja — all reach the shell after the dev-shell launch. `cl` needs the dev shell (INCLUDE/LIB), not just PATH.
- No submodules present yet (`.gitmodules` empty). Task 1 adds `external/vcpkg`.
- Old rcedit v1 tree present under src/ (src/cmd, src/utils, src/main.cpp) + top-level CMakeLists.txt — Task 1 deletes it.
- Commit identity: `git -c user.name=fabienfl -c user.email=fabien.fl-orc@ssi.gouv.fr commit ...` (do not write to config).

## Pre-flight conflict scan
Full tables: preflight-scan.md. Verdict: interfaces match end-to-end; 20 intra-task/plan-vs-spec defects. Rulings below (spec = binding authority; TODO decisions cited where plan contradicts itself). Note: scan agent spawned its own research subagents (one overwrote the report file); result hand-verified and sound. Implementers carry the no-subagents contract.

### Rulings (rule on each before execution)
- Ruling R1 (#1 exe output path) — Task 1 sets `set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")` at top level so rcedit.exe / rcedit_tests.exe land at `build/<preset>/<Config>/`, making T1 Step-10 and all T11 CI/PDB paths correct as written. Cost if wrong: exe path mismatch, caught at T1/T11 build.
- Ruling R2 (#3 RC language) — Task 1 uses `project(rcedit VERSION 2.0.0 LANGUAGES C CXX RC)` (enable RC) so T7's fixture.rc compiles under Ninja Multi-Config. Cost if wrong: T7 configure fails.
- Ruling R3 (#17 T11 PDB / Files-header gap) — Task 1 front-loads PDB generation for release: `/Zi` compile + linker `/DEBUG /OPT:REF /OPT:ICF`, so rcedit.pdb exists for MinSizeRel and T11 need not edit CMakeLists. Cost if wrong: no PDB for artifact, caught at T11.
- Ruling R4 (#2) — Task 6 uses `HRESULT_FROM_WIN32(ERROR_NEGATIVE_SEEK)` (the plan's `HRESULT_WIN32_ERROR_NEGATIVE_SEEK` is undefined). Cost: compile fail at T6.
- Ruling R5 (#5 comsuppw / link policy) — The IMPORT ALLOWLIST (KERNEL32 always; OLEAUT32 only w/ 7z) is the binding gate, enforced by the `imports` CTest. Static libs that add NO new DLL import (uuid.lib; comsuppw.lib if the linker demands `_com_issue_error`) are PERMITTED with 7z — consistent with TODO decision (line 33). "Nothing else, ever" = no new DLL-importing lib / no new DLL in the import table. If a link pulls a symbol requiring a NEW DLL, STOP and report symbol names. Implementer must report if it adds comsuppw and confirm the imports CTest still passes. Binds T1 (link list = kernel32+CRT, +oleaut32+uuid w/ 7z), T6, T10. Cost: at worst a spurious stop or an unexpected import — the imports CTest catches any real DLL import regardless.
- Ruling R6 (#6 zstd target) — Task 5 prefers `zstd::libzstd_static` (spec + static-only); fall back to `zstd::libzstd` only if the former isn't exported; verify against installed zstd config and report which was used. Cost: link fail at T5.
- Ruling R7 (#7/#10 T8 hexdump) — Task 8 derives the GOLDEN hexdump output for the 7-byte "fixture" from the spec's format (standard `hexdump -C`/xxd style), then makes BOTH impl and test match it. Do not trust the plan's 31-space test literal nor the 30-space impl blindly. Cost: red test, caught at T8.
- Ruling R8 (#8 T6 Seek dup) — Task 6 factors the shared seek math into one helper used by both streams. Quality, low cost.
- Ruling R9 (#9 7z entry name) — Keep fixed internal entry name `L"payload"`; decompression reads entry index 0, so the name never surfaces. Codec::Compress has no name param; document the deviation in a comment. Cost: none (cosmetic archive metadata).
- Ruling R10 (#10 case-fold dup) — Task 2 adds `EqualsIgnoreCase(wstring_view,wstring_view)` to encoding.h/.cpp (consistent case-folding via towlower); Task 3 and Task 4 consume it instead of each reimplementing. Cost: small added surface in T2.
- Ruling R11 (#11 T9 label dup + magic 18) — Task 9 extracts one option-label formatter and computes column width from content, not a hardcoded 18. Cost: minor.
- Ruling R12 (#12 T10 ParseLimit overflow + unchecked expected) — Task 10 guards ParseLimit against size_t overflow (usage error on overflow, mirroring ParseLang's bound check) and has Run* handlers check `.has_value()` defensively rather than rely solely on validate. Cost: minor robustness.
- Ruling R13 (#13/#4/#16 undercounted deviations) — ACCEPTED intended deviations (internally consistent, no code change): `ParseResourceName/FormatResourceName` naming; `OptionSpec.valueName` 6th field; `ParsedArgs.values` key type; `errc::empty_payload`. Added to the intended-deviation list handed to reviewers so they aren't flagged as spec violations. Cost: none (doc).
- Ruling R14 (#14 zstd checksum) — Advanced CCtx API + `ZSTD_c_checksumFlag=1` allowed (frames are self-produced; adds integrity); require an explanatory code comment. Cost: negligible.
- Ruling R15 (#17 four formatters) — YAGNI: deliver only `FormatError` + `formatter<ResourceId>` (the used ones); do NOT add unused formatter<ResourceKey/error_code/path>. Accepted deviation. Cost: none; add later if needed.
- Ruling R16 (#15 CI artifact) — Task 11 gates artifact upload on `startsWith(github.ref,'refs/tags/')` AND preset==default (spec: "on tags"); avoid unsanitized ref in artifact name. Cost: none.
- Ruling R17 (#7 top / T11 README) — Task 11 README states presets accurately: only `default` has `-Debug` and `-RelWithDebInfo`; no-7z/no-zstd/minimal have `-MinSizeRel` only — describe from what T1 actually produced. Cost: doc accuracy.
- Ruling R18 (#18 T4 under-asserted test) — Task 4's `FindCodecMatchesBuildFlags` None case asserts the specific documented error code, not just `!has_value()`. Cost: minor test rigor.

## Execution log
BASE (branch start) = f5afff952332843bf160e4344edb45e32f934857
Task 1: dispatched (implementer agent a9c688ddc905e4036, sonnet) with rulings R1/R2/R3 folded in + R5 link note + corrected dev-shell path. BASE for review = f5afff9.
Task 1: implementer DONE, commit ec147cc. version+imports CTests pass on minimal-MinSizeRel and default-MinSizeRel; imports = KERNEL32 only on both (nothing calls 7z yet — correct). Self-noted: added explicit CMAKE_CONFIGURATION_TYPES incl. MinSizeRel (Ninja MC omits it by default); wrapped import check in rcedit_add_import_check(); GIT_CONFIG_GLOBAL scratch override for network-share submodule ownership (no persistent config). Reviewer dispatched (agent a98ea543217a5a6b0, sonnet).
Note: repo clangd has no C++23/include config → emits false positives (std::print/span/string_view "not found", nested-namespace "C++17"). Authoritative = MSVC build passing. Ignore clangd standard/include diagnostics for all tasks.
Task 1: review clean — Spec ✅, quality Approved. Task 1: minor (deferred): CMakeLists CMAKE_CONFIGURATION_TYPES uses CACHE ... FORCE, overwrites user -D override; prefer `if(NOT CMAKE_CONFIGURATION_TYPES)`.
Task 1: complete (commits f5afff9..ec147cc, review clean).
Task 2: dispatched (implementer agent ad10e554e6f90b35b, sonnet) with delta R10 (EqualsIgnoreCase in encoding). BASE for review = ec147cc. Awaiting report.

Task 2: implementer DONE, commit b67129f. 4/4 CTest groups (version, error, encoding, imports) pass on minimal+default MinSizeRel, pristine. R10 EqualsIgnoreCase in place; folds ASCII only (towlower/"C" locale) — fine for ASCII consumers (aliases, codec names); noted for T3/T4. Reviewer dispatched (agent a10974d03e1ecf271, sonnet). BASE for review = ec147cc.
Task 2: review clean — Spec ✅, Approved. Minors (deferred, both match brief, not defects): guard.h has no automated test yet (guards verified later); FileHandle is hand-rolled RAII lacking release()/reset() (may matter for T7 engine).
Task 2: complete (commits ec147cc..b67129f, review clean).
Task 3: dispatched (implementer agent acd0360d182c2f605, sonnet). Delta: consume R10 EqualsIgnoreCase; accepted deviations R13/R15. BASE for review = b67129f. Awaiting report.
Task 3: implementer DONE, commit 89ccdcf. 5/5 CTest groups pass on minimal+default; resource_id 14/14; pristine. Consumed EqualsIgnoreCase, no dup case-fold. Reviewer dispatched (agent a54e8e1afec229d8d, sonnet). BASE for review = b67129f.
Task 3: review clean — Spec ✅, Approved, no issues.
Task 3: complete (commits b67129f..89ccdcf, review clean).
Task 4: dispatched (implementer agent aad1bb90cd26dd596, sonnet). Deltas R10 (EqualsIgnoreCase in ParseCodecName), R18 (None asserts specific errc). BASE for review = 89ccdcf. Awaiting report.
NOTE (build gating): codecs come online incrementally, so the linkable preset per task is:
  after T4: minimal only (FindCodec refs undefined ZstdCodec/SevenZipCodec under enabled presets — EXPECTED).
  after T5 (zstd): minimal + no-7z.  after T6 (7z): all four (default, no-7z, no-zstd, minimal).
  Gate each codec task on its linkable preset; full default/no-zstd validation happens at T6.
Task 4: implementer DONE, commit 3e7cfed. minimal 6/6 CTest groups pass (codec 4 cases) pristine; default compiles guarded branches, tests exe fails to LINK on undefined ZstdCodec/SevenZipCodec — EXPECTED (T5/T6 add them). Reviewer dispatched (agent a81397c1e4e8f3f4a, sonnet). BASE for review = 89ccdcf.
Task 4: review clean — Spec ✅, Approved, no issues. R18 None case asserts std::errc::invalid_argument (documented contract). Guards use RCEDIT_HAS_ZSTD/RCEDIT_HAS_7Z (derived from CMake options).
Task 4: complete (commits 89ccdcf..3e7cfed, review clean).
Task 5: dispatched (implementer agent af83697f4f8eebab7, sonnet). Deltas R6 (static zstd target), R14 (checksum comment). Gate no-7z. BASE for review = 3e7cfed. Awaiting report.
Task 5: implementer DONE, commit 7d5531a. no-7z 6/6 (ZstdRoundTrip/RejectsEmpty/RejectsCorrupt), minimal 6/6 (zstd out), imports=KERNEL32 only, pristine. R6: linked zstd::libzstd_static (both exported; chose static per ruling, commented). R14: checksum comment added. Reviewer dispatched (agent a8ae6afaf383df572, sonnet). BASE for review = 3e7cfed.
Task 5: review — Spec compliant on rulings, but ONE Important finding: codec_zstd.cpp streaming-decompress (unknown-content-size branch) ignores last ZSTD_decompressStream return (0=complete vs >0=incomplete), so a truncated frame lacking embedded content size returns success instead of errc::corrupt_payload. Latent (self-produced frames embed content size → other branch). Enters fix loop. FIX_BASE = 7d5531a.
Task 5: fix round 1 DONE, commit e861749 (track last ZSTD_decompressStream rc; new tests ZstdStreamingFrameRoundTrip/RejectsTruncation, verified red first). no-7z codec 9/9, minimal 6/6. Scoped re-review dispatched (agent aaa0a47c733f8259e, sonnet). FIX_BASE=7d5531a HEAD=e861749.
Task 5: fix round 1 re-review — ADDRESSED (last-rc tracking correct, scoped to streaming branch, test verified red first), no new breakage.
Task 5: complete (commits 3e7cfed..e861749, review clean after 1 fix round).
Task 6: dispatched (implementer agent a9c7ff893053ffd81, sonnet). Deltas R4/R8/R9/R5. Gate default+no-zstd. BASE for review = e861749. Awaiting report (7z builds from source first time).
Task 6: implementer agent a9c7ff893053ffd81 was KILLED (stopped by user) mid-work while verifying the no-task6 link failure. HEAD unchanged at e861749; working tree CLEAN; Task 6 WIP preserved in stash@{0} ("WIP on master: e861749"). No commits made. Paused to check with user (user-initiated stop = intervention signal). Do NOT drop stash@{0} — it holds the Task 6 progress.
Task 6: RESUMED — fresh implementer agent a49be9333d7708f7a (opus) to pop stash@{0} and finish (verify COM sigs, apply R4/R8/R9/R5, add tests, build default+no-zstd, confirm imports). BASE for review = e861749. Awaiting report.
Task 6: RESUMED — DONE. Code commit 73e9081 (12 files, 817 lines). Stash consumed. Implementer had ALSO committed the SDD report into the repo (efc9d8b) — dropped that commit (soft reset + unstage); report kept as ignored scratch, HEAD now 73e9081. Recovered WIP was "already correct": modern Z7_IFACE_COM7_IMP/Z7_COM7F_IMF idiom matching installed 7-Zip 24.06 headers (not the brief's STDMETHOD snippets); R4/R8/R9 already applied; R5 comsuppw NOT needed. imports=KERNEL32 only (rcedit.exe doesn't call the 7z codec until Task 10 wires commands, so OLEAUT32 appears later; KERNEL32-only is a valid subset of the allowlist now). SevenZip round-trip/ContentSize/empty/corrupt test cases already existed from Task 4 and now link+pass. Reviewer dispatched. BASE for review = e861749.
Task 6: review — Spec ✅ (reviewer rebuilt no-zstd from clean + reran ctest/dumpbin; all rulings R4/R8/R9/R5 confirmed; COM refcounting clean). ONE Important finding: out_mem_stream.cpp Write(:39)/SetSize(:74) catch only std::bad_alloc; vector::resize also throws std::length_error when size > max_size(). Corrupt archive with outsized kpidSize → SetSize(huge) → length_error escapes COM boundary out of Decompress (violates no-exceptions-across-boundary). Existing SevenZipRejectsCorrupt only bit-flips, doesn't hit this. Enters fix loop. FIX_BASE = 73e9081.
Task 6: fix round 1 dispatched (resume implementer a49be9333d7708f7a, opus).
Task 6: fix round 1 DONE, code commit 3c5db96 (broadened OutMemStream Write/SetSize catch bad_alloc->std::exception, returns E_OUTOFMEMORY -> surfaces errc::corrupt_payload; new guarded tests/test_sevenzip.cpp 'sevenzip' group: SetSize(UINT64_MAX)+oversized Write; TDD: pre-fix group fast-fails 0xC0000409, post-fix 2/2 pass). no-zstd+default 7/7, no-7z/minimal 6/6, /WX clean. Implementer AGAIN committed the report (a75a893) -> dropped it (soft reset+unstage). Scoped re-review dispatched. FIX_BASE=73e9081 HEAD=3c5db96.
Task 6: fix round 1 re-review — ADDRESSED (both Write/SetSize catch std::exception -> E_OUTOFMEMORY; covering test drives SetSize(UINT64_MAX)/oversized Write asserting FAILED HRESULT + no throw across COM; registration guarded), no new breakage.
Task 6: complete (commits e861749..3c5db96, review clean after 1 fix round).

=== PAUSED after Task 6 at user request ===
Remaining: Task 7 (engine + Win32 impl), Task 8 (operations), Task 9 (arg parser), Task 10 (commands/main/smoke), Task 11 (CI/README). Briefs 7 already staged. To resume: continue subagent-driven from Task 7. Rulings still pending for later tasks: R11 (T9), R12 (T10), R13/R15 (accepted deviations), R16/R17 (T11), R7 (T8 hexdump golden), R5 (T10 link). Do NOT run final whole-branch review or delete workspace until Task 11 done.
NOTE for future implementers: two implementers committed the SDD report file into the repo (dropped both times). Tell implementers NOT to `git add` anything under .superpowers/ — it's scratch (ignored via .superpowers/sdd/.gitignore `*`); the report goes to the report FILE only, not a commit.

=== CROSS-MACHINE HANDOFF (committed to git 2026-09-03) ===
This SDD workspace is now committed so the plan can be resumed on another machine with no access to this one. MACHINE-SPECIFIC facts above must be re-derived on the new machine:
- VS dev-shell path (this machine: VS 18 BuildTools at "C:\Program Files (x86)\...\18\BuildTools\...") — find the local Launch-VsDevShell.ps1; needs C++23 MSVC.
- Absolute S:\ paths (repo on a network share; reference projects S:\llm\dfir-orc-forge and S:\llm\orc\...\OrcCapsule) will NOT exist. The 7zip overlay port is already vendored under external/vcpkg_overlay_ports/ (Task 6 done), so those reference projects are no longer needed for Tasks 7-11. Ignore any brief text pointing at them.
- external/vcpkg is a submodule pinned to cd61e1e2... — run `git submodule update --init` after clone. First configure of a codec preset rebuilds 7-Zip+zstd from source (minutes).
- Git identity not configured: commit with `git -c user.name=fabienfl -c user.email=fabien.fl-orc@ssi.gouv.fr commit ...`.
RESUME: re-invoke superpowers:subagent-driven-development on plan docs/superpowers/plans/2026-09-03-rcedit-v2.md; this ledger's "Task N: complete" lines mark 1-6 done; start at Task 7 (brief already staged as task-7-brief.md). Regenerate review packages with the skill's review-package script as needed. Keep the accepted-deviation and pending rulings above.
