# rcedit backlog

Features identified on 2026-09-04 by reviewing the shipped command set against
what a caller actually needs. Nothing here is scheduled; the order is by value
to a user, not by effort. Each entry says what is missing, why it matters,
where it lands in the tree, and roughly how big it is.

Entry 10 is the largest by far and sets the scope of the tool: the decision is
made, and it is to match Electron's rcedit.

## 1. Streaming: `get` to stdout, `set` from stdin -- medium

`get` fails without `--output FILE` (`src/cli/Cmd/CmdGet.cpp`), and `set`
accepts text or a path but no `-`. The pipeline forms therefore do not exist:

```
rcedit get app.exe -t RCDATA -n CONFIG -o - | jq .
gzip -dc payload.gz | rcedit set app.exe -t RCDATA -n BLOB --value-path -
```

Every extract-modify-reinject loop has to invent a temporary file and clean it
up. `Out::Write` takes a `wstring_view` and encodes to UTF-8, so this is not a
matter of special-casing a filename: it needs a raw byte writer on stdout, a
raw byte reader on stdin, and both handles switched to binary mode so a `0x0A`
in a payload is not turned into `0x0D 0x0A`. The confirmation block must go to
stderr, or be suppressed, whenever stdout carries the payload.

Open question: `-` as the convention, or an explicit `--stdout` / `--stdin`
pair. `-` is what callers will try first.

## 2. Machine-readable `list` -- small

`list` prints a padded table whose last column is a preview of arbitrary
printable bytes, spaces and quotes included. Nothing downstream can split that
line reliably, which makes the tool unscriptable at its most useful point.

`ListEntry` already holds the whole record, so a `--json` form is close to
free: type, name, lang, size, codec, unpacked size, and the preview as a byte
array or an escaped string. A NUL-separated `--porcelain` variant would serve
shell callers who do not want a JSON parser. This was already noted as future
work in the v2 spec.

## 3. Batch operations -- medium

Every command works on exactly one resource, keyed by an exact `--type` and
`--name`, even though `list` already has the vocabulary for filtering. Missing:

- `get --all -o <dir>`, writing one file per resource named by type, name and
  language.
- `remove` driven by the same filters `list` accepts.
- `set` of several resources in one pass.

Beyond convenience this is a cost question. With `--output`, each `set` runs
its own `CopyToOutput` and its own `EndUpdateResourceW` cycle
(`src/core/ops.cpp`), so N resources means N full copies of the image and N
commit cycles, with every intermediate state left on disk. A batch mode copies
once and commits once.

## 4. In-place edits have no way back -- small

`RemoveStrayOutput` cleans up only the `--output` copy. In place, the target
*is* the original: a failure inside `Commit()` -- disk full, a torn write --
leaves the file however Windows left it, with nothing to restore from. Write
to a sibling temporary and rename over the original, or offer `--backup`.
`Discard()` already covers the failures that happen before the commit, so this
is only about the commit itself.

## 5. `set` can silently fork a resource into two languages -- small

`get`, `remove` and `hexdump` resolve a bare `--name` to the neutral resource,
then to the only existing language. `Set` does neither: it forces `lang = 0`
(`src/core/ops.cpp`). So reading a resource stored under 1033, editing it and
setting it back creates a *second* entry at lang 0, and the next `get` fails
with `ambiguous_language` on a file the caller thought they had round-tripped.

The behaviour is documented and defensible -- `set` creates, it does not
resolve -- but it should warn when it adds a language to a name that already
exists under another. A `--lang-any` that resolves the way `get` does would
make the round trip work without a warning to ignore.

## 6. No compression level -- small

`-c zstd` and `-c 7z` take no level. For an embedded payload of any size the
difference between the default and the maximum is worth having, and it costs
nothing at read time since the codec is detected by magic. `-c zstd:19`, or a
separate `--level`, applied in `Set` where the codec is looked up.

## 7. `--lang` filter on `list` -- small

`list` filters on type and name but not on language, so there is no way to ask
what a given language holds on a file that carries several. `ListOptions`
gains a third optional field and the filter follows the two already there.

## 8. Rename, retype, relanguage without a round trip -- small

Changing the name, type or language of a resource means `get` to a file, `set`
under the new key, `remove` the old one. A `move` command would do it in one
commit, and would be the natural home for the language fix in entry 5.

## 9. `set` rejects an empty payload -- trivial

`Set` returns `errc::empty_payload` for zero bytes, so a resource that exists
and is empty cannot be created. Probably deliberate, since an empty resource is
usually a mistake, but a `--allow-empty` would remove a special case a caller
cannot work around.

## 10. Structured resources: parity with Electron's rcedit -- large

**Decided: implement it.** This tool shares a name with Electron's rcedit, and
should do what that one does. Today it cannot: those resources are not raw
payloads, so `set -t RT_ICON --value-path app.ico` produces something Windows
will not render, with no error to say why.

The surface to match, one option at a time:

| Option | Resource | What it takes |
| --- | --- | --- |
| `--set-icon <ico>` | `RT_GROUP_ICON` + `RT_ICON` | An `.ico` splitter: one numbered `RT_ICON` per image, plus the group directory that indexes them. Replacing an icon means removing the images the old group pointed at, or the file grows on every run. |
| `--set-version-string <key> <value>` | `RT_VERSION` | A `VS_VERSIONINFO` builder: the fixed `VS_FIXEDFILEINFO`, then the `StringFileInfo` table for the language and codepage, then `VarFileInfo`. Every block is length-prefixed and 32-bit aligned, which is where implementations usually get it wrong. |
| `--get-version-string <key>` | `RT_VERSION` | The parser for the same structure. Needed anyway to edit one key without dropping the others. |
| `--set-file-version <v>` | `RT_VERSION` | `FILEVERSION` in the fixed info, and the matching `FileVersion` string. |
| `--set-product-version <v>` | `RT_VERSION` | `PRODUCTVERSION`, and the matching `ProductVersion` string. |
| `--set-resource-string <id> <value>` | `RT_STRING` | String tables come in bundles of sixteen: id `n` lives at index `n % 16` of resource `n / 16 + 1`, each entry a UTF-16 length prefix followed by unterminated characters. Setting one string means rewriting its whole bundle. |
| `--application-manifest <file>` | `RT_MANIFEST` | Close to a raw set, but the id and language must be the ones the loader looks for. |
| `--set-requested-execution-level <level>` | `RT_MANIFEST` | `asInvoker`, `highestAvailable` or `requireAdministrator`, edited into the existing manifest XML rather than replacing it -- which needs enough of an XML rewriter to touch one attribute and leave everything else byte-identical. |

Notes for whoever picks this up:

- Every one of these is read-modify-write on a structure that already exists in
  the file, so the parsers are not optional: a writer alone would silently drop
  the fields it does not know about.
- The last two subsume the "manifest editing" item carried over from the v2
  handoff. It is not a separate piece of work.
- Electron's rcedit is MIT and small; reading it is the fastest way to get the
  structure layouts right. Do not vendor it -- the import allowlist and the
  static-only build are ours to keep.
- These options are per-file, not per-resource, so they do not fit the
  `--type` / `--name` shape of the existing commands. Decide early whether they
  hang off `set` or become their own commands.

## 11. Recompute the PE checksum -- medium

Already a TODO in `src/core/engine_win32.cpp`, with the algorithm noted. The
checksum is cleared rather than recomputed after an update, which the loader
accepts for anything but a driver or a boot-time DLL; `editbin /release` is the
documented workaround. `imagehlp!CheckSumMappedFile` would do it but pulls in a
DLL outside the import allowlist, so it has to be written in-tree.

## Carried over from the v2 handoff

- Portable PE engine, so the tool builds and runs on Linux. `ResourceEngine`
  exists to make this possible; nothing else has been done.
- Manifest editing, now folded into entry 10 -- `--application-manifest` and
  `--set-requested-execution-level` are that work.

Both were listed as future work in
`docs/superpowers/plans/TODO.md` and are recorded here so this file is the only
place to look.
