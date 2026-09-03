### Task 8: Operations (List, Get, Set, Remove, Hexdump)

**Files:**
- Create: `src/core/ops.h`, `src/core/ops.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/main.cpp`
- Test: `tests/test_ops.cpp`

**Interfaces:**
- Consumes: `ResourceEngine`, `ResourceEntry`, `OpenMode`, `ResourceKey`, `Codec`, `CodecId`, `DetectCodec`, `FindCodec`, `IsCodecAvailable`, `errc`, `Log`.
- Produces:
  ```cpp
  struct ListOptions { std::optional<ResourceId> type; std::optional<ResourceId> name; };
  struct ListEntry { ResourceKey key; uint32_t size; CodecId codec; std::optional<uint64_t> contentSize; };
  std::error_code List(ResourceEngine&, const fs::path& pe, const ListOptions&, std::vector<ListEntry>& out);
  std::error_code Get(ResourceEngine&, const fs::path& pe, const ResourceKey&, bool raw, std::vector<uint8_t>& out);
  std::error_code Set(ResourceEngine&, const fs::path& pe, const ResourceKey&, std::span<const uint8_t> data, CodecId, const std::optional<fs::path>& output);
  std::error_code Remove(ResourceEngine&, const fs::path& pe, const ResourceKey&, const std::optional<fs::path>& output);
  std::error_code Hexdump(ResourceEngine&, const fs::path& pe, const ResourceKey&, bool raw, std::optional<size_t> limit, std::wstring& out);
  // Exposed for tests and the CLI error message:
  std::error_code ResolveLanguage(ResourceEngine& openEngine, const ResourceKey&, ResourceKey& resolved, std::vector<uint16_t>& candidates);
  std::wstring FormatHexdump(std::span<const uint8_t> data, std::optional<size_t> limit);
  bool IsRunningExecutable(const fs::path&);
  ```

Rules:
- Language resolution (`Get`, `Hexdump`, `Remove`): `key.lang` set is used as-is. Unset: enumerate, collect the languages of entries matching type and name. None: `resource_not_found`. Contains 0: neutral. Exactly one: that one. Otherwise `ambiguous_language`, with `candidates` filled for the CLI message. `Set` with no language writes neutral (0).
- `Get`/`Hexdump`: after reading, `DetectCodec`. `None` or `raw`: return stored bytes. Otherwise `FindCodec` (a disabled codec propagates `codec_disabled`) and `Decompress`.
- `Set`: empty data is `empty_payload`. Target is `output` if given, else `pe`. If `IsRunningExecutable(target)`: `self_update`. With `output`: `copy_file(pe, output, overwrite_existing)` first, never open `pe` for writing. Compress when codec is not `None`. Open target `ReadWrite`, `Write`, `Commit`.
- `Remove`: same target and copy logic as `Set`; open `ReadWrite`, resolve language (reads happen before the first write), `Remove`, `Commit`.
- `List`: enumerate, apply filters, `DetectCodec` on each payload (requires a `Read` per entry), `contentSize` from the codec when available, `nullopt` otherwise.
- `FormatHexdump`: 16 bytes per line: `{:08X}  ` then hex pairs separated by spaces with a double space after the 8th byte, padded to 49 columns, then `|ascii|` where non-printable bytes are `.`. Empty data prints `<empty>`. With `limit` smaller than the size, only `limit` bytes are shown followed by a line `... ({} more bytes)`.

- [ ] **Step 1: Write failing tests**

`tests/test_ops.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"
#include "temp.h"

#include <windows.h>

#include "core/codec.h"
#include "core/engine.h"
#include "core/error.h"
#include "core/ops.h"

namespace rcedit::test {

namespace {

const ResourceId kRcData(uint16_t(10));
const ResourceKey kBaseline{kRcData, ResourceId(std::wstring(L"FIXTURE")), 0};
const ResourceKey kConfigNoLang{kRcData, ResourceId(std::wstring(L"CONFIG")), std::nullopt};
const ResourceKey kConfigNeutral{kRcData, ResourceId(std::wstring(L"CONFIG")), 0};
const ResourceKey kConfigFr{kRcData, ResourceId(std::wstring(L"CONFIG")), 1036};
const ResourceKey kConfigEn{kRcData, ResourceId(std::wstring(L"CONFIG")), 1033};

std::vector<uint8_t> Bytes(std::string_view s)
{
    return std::vector<uint8_t>(s.begin(), s.end());
}

std::vector<uint8_t> Pattern(size_t size)
{
    std::vector<uint8_t> v(size);
    for (size_t i = 0; i < size; ++i)
    {
        v[i] = static_cast<uint8_t>((i * 13) % 251);
    }
    return v;
}

std::vector<ListEntry> ListAll(const std::filesystem::path& pe)
{
    auto engine = MakeWin32Engine();
    std::vector<ListEntry> entries;
    CHECK_EC_OK(List(*engine, pe, ListOptions{}, entries));
    return entries;
}

const ListEntry* Find(const std::vector<ListEntry>& entries, const ResourceKey& key)
{
    for (const auto& e : entries)
    {
        if (e.key == key)
        {
            return &e;
        }
    }
    return nullptr;
}

void ListBaseline()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    const auto entries = ListAll(pe);
    CHECK(entries.size() == 1);
    const auto* e = Find(entries, kBaseline);
    CHECK(e != nullptr && e->size == 7 && e->codec == CodecId::None && !e->contentSize.has_value());
}

void ListFilters()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    auto engine = MakeWin32Engine();
    std::vector<ListEntry> entries;

    CHECK_EC_OK(List(*engine, pe, ListOptions{kRcData, std::nullopt}, entries));
    CHECK(entries.size() == 1);

    CHECK_EC_OK(List(*engine, pe, ListOptions{ResourceId(uint16_t(3)), std::nullopt}, entries));
    CHECK(entries.empty());

    CHECK_EC_OK(List(*engine, pe, ListOptions{std::nullopt, ResourceId(std::wstring(L"FIXTURE"))}, entries));
    CHECK(entries.size() == 1);

    CHECK_EC_OK(List(*engine, pe, ListOptions{std::nullopt, ResourceId(std::wstring(L"NOPE"))}, entries));
    CHECK(entries.empty());
}

void SetThenGetUncompressed()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    auto engine = MakeWin32Engine();
    const auto payload = Bytes("<config/>");

    CHECK_EC_OK(Set(*engine, pe, kConfigNoLang, payload, CodecId::None, std::nullopt));

    const auto entries = ListAll(pe);
    CHECK(entries.size() == 2);
    const auto* e = Find(entries, kConfigNeutral);
    CHECK(e != nullptr && e->size == payload.size() && e->codec == CodecId::None);

    std::vector<uint8_t> out;
    CHECK_EC_OK(Get(*engine, pe, kConfigNoLang, false, out));
    CHECK(out == payload);
    CHECK_EC_OK(Get(*engine, pe, kConfigNeutral, true, out));
    CHECK(out == payload);
}

void SetEmptyFails()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    auto engine = MakeWin32Engine();
    CHECK(Set(*engine, pe, kConfigNoLang, std::span<const uint8_t>(), CodecId::None, std::nullopt) == errc::empty_payload);
}

void SetWithOutputLeavesSourceUnchanged()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    const auto out = dir.Path() / L"b.exe";
    auto engine = MakeWin32Engine();

    CHECK_EC_OK(Set(*engine, pe, kConfigNoLang, Bytes("x"), CodecId::None, out));

    CHECK(ListAll(pe).size() == 1);
    CHECK(ListAll(out).size() == 2);
}

void SetRefusesRunningExecutable()
{
    wchar_t self[MAX_PATH];
    ::GetModuleFileNameW(nullptr, self, MAX_PATH);
    CHECK(IsRunningExecutable(self));

    auto engine = MakeWin32Engine();
    CHECK(Set(*engine, self, kConfigNoLang, Bytes("x"), CodecId::None, std::nullopt) == errc::self_update);
    CHECK(Remove(*engine, self, kBaseline, std::nullopt) == errc::self_update);
}

void CompressedRoundTrip(CodecId codec)
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    auto engine = MakeWin32Engine();
    const auto payload = Pattern(50000);

    CHECK_EC_OK(Set(*engine, pe, kConfigNoLang, payload, codec, std::nullopt));

    const auto entries = ListAll(pe);
    const auto* e = Find(entries, kConfigNeutral);
    CHECK(e != nullptr);
    CHECK(e->codec == codec);
    CHECK(e->size < payload.size());
    CHECK(e->contentSize.has_value() && *e->contentSize == payload.size());

    std::vector<uint8_t> raw;
    CHECK_EC_OK(Get(*engine, pe, kConfigNoLang, true, raw));
    CHECK(DetectCodec(raw) == codec);
    CHECK(raw.size() == e->size);

    std::vector<uint8_t> unpacked;
    CHECK_EC_OK(Get(*engine, pe, kConfigNoLang, false, unpacked));
    CHECK(unpacked == payload);
}

void DisabledCodecIsRejectedOnSet(CodecId codec)
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    auto engine = MakeWin32Engine();
    CHECK(Set(*engine, pe, kConfigNoLang, Bytes("x"), codec, std::nullopt) == errc::codec_disabled);
    CHECK(ListAll(pe).size() == 1);
}

void DisabledCodecIsRejectedOnGet(std::vector<uint8_t> magicPrefixed, CodecId codec)
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    auto engine = MakeWin32Engine();
    CHECK_EC_OK(Set(*engine, pe, kConfigNoLang, magicPrefixed, CodecId::None, std::nullopt));

    const auto entries = ListAll(pe);
    const auto* e = Find(entries, kConfigNeutral);
    CHECK(e != nullptr && e->codec == codec && !e->contentSize.has_value());

    std::vector<uint8_t> out;
    CHECK(Get(*engine, pe, kConfigNoLang, false, out) == errc::codec_disabled);
    CHECK_EC_OK(Get(*engine, pe, kConfigNoLang, true, out));
    CHECK(out == magicPrefixed);
}

#ifdef RCEDIT_HAS_ZSTD
void ZstdRoundTrip()
{
    CompressedRoundTrip(CodecId::Zstd);
}
#else
void ZstdDisabledOnSet()
{
    DisabledCodecIsRejectedOnSet(CodecId::Zstd);
}
void ZstdDisabledOnGet()
{
    DisabledCodecIsRejectedOnGet({0x28, 0xB5, 0x2F, 0xFD, 1, 2, 3, 4}, CodecId::Zstd);
}
#endif

#ifdef RCEDIT_HAS_7Z
void SevenZipRoundTrip()
{
    CompressedRoundTrip(CodecId::SevenZip);
}
#else
void SevenZipDisabledOnSet()
{
    DisabledCodecIsRejectedOnSet(CodecId::SevenZip);
}
void SevenZipDisabledOnGet()
{
    DisabledCodecIsRejectedOnGet({0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C, 1, 2}, CodecId::SevenZip);
}
#endif

void LanguageResolution()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    auto engine = MakeWin32Engine();
    std::vector<uint8_t> out;

    // No CONFIG at all.
    CHECK(Get(*engine, pe, kConfigNoLang, false, out) == errc::resource_not_found);

    // One language: resolves to it.
    CHECK_EC_OK(Set(*engine, pe, kConfigFr, Bytes("fr"), CodecId::None, std::nullopt));
    CHECK_EC_OK(Get(*engine, pe, kConfigNoLang, false, out));
    CHECK(out == Bytes("fr"));

    // Two non-neutral languages: ambiguous, candidates listed.
    CHECK_EC_OK(Set(*engine, pe, kConfigEn, Bytes("en"), CodecId::None, std::nullopt));
    CHECK(Get(*engine, pe, kConfigNoLang, false, out) == errc::ambiguous_language);
    {
        auto ro = MakeWin32Engine();
        CHECK_EC_OK(ro->Open(pe, OpenMode::ReadOnly));
        ResourceKey resolved;
        std::vector<uint16_t> candidates;
        CHECK(ResolveLanguage(*ro, kConfigNoLang, resolved, candidates) == errc::ambiguous_language);
        CHECK(candidates.size() == 2);
    }

    // Neutral present: wins.
    CHECK_EC_OK(Set(*engine, pe, kConfigNoLang, Bytes("neutral"), CodecId::None, std::nullopt));
    CHECK_EC_OK(Get(*engine, pe, kConfigNoLang, false, out));
    CHECK(out == Bytes("neutral"));

    // Explicit language always wins.
    CHECK_EC_OK(Get(*engine, pe, kConfigEn, false, out));
    CHECK(out == Bytes("en"));
}

void RemoveResolvesLanguage()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    auto engine = MakeWin32Engine();

    CHECK_EC_OK(Set(*engine, pe, kConfigFr, Bytes("fr"), CodecId::None, std::nullopt));
    CHECK(ListAll(pe).size() == 2);

    CHECK_EC_OK(Remove(*engine, pe, kConfigNoLang, std::nullopt));
    CHECK(ListAll(pe).size() == 1);

    CHECK(Remove(*engine, pe, kConfigNoLang, std::nullopt) == errc::resource_not_found);
}

void RemoveWithOutput()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    const auto out = dir.Path() / L"b.exe";
    auto engine = MakeWin32Engine();

    CHECK_EC_OK(Remove(*engine, pe, kBaseline, out));
    CHECK(ListAll(pe).size() == 1);
    CHECK(ListAll(out).empty());
}

void HexdumpFormatting()
{
    const auto data = Bytes("fixture");
    const std::wstring line = FormatHexdump(data, std::nullopt);
    CHECK(line == L"00000000  66 69 78 74 75 72 65                               |fixture|\n");

    CHECK(FormatHexdump(std::span<const uint8_t>(), std::nullopt) == L"<empty>\n");

    std::vector<uint8_t> twenty(20, 0x41);
    twenty[7] = 0x00;
    const std::wstring two = FormatHexdump(twenty, std::nullopt);
    CHECK(two.starts_with(L"00000000  41 41 41 41 41 41 41 00  41 41 41 41 41 41 41 41  |AAAAAAA.AAAAAAAA|\n"));
    CHECK(two.find(L"00000010  41 41 41 41") != std::wstring::npos);

    const std::wstring limited = FormatHexdump(twenty, 4);
    CHECK(limited.starts_with(L"00000000  41 41 41 41"));
    CHECK(limited.find(L"... (16 more bytes)") != std::wstring::npos);
}

void HexdumpOfResource()
{
    TempDir dir;
    const auto pe = CopyFixture(dir, L"a.exe");
    auto engine = MakeWin32Engine();
    std::wstring text;
    CHECK_EC_OK(Hexdump(*engine, pe, kBaseline, false, std::nullopt, text));
    CHECK(text.starts_with(L"00000000  66 69 78 74 75 72 65"));
}

constexpr TestCase kCases[] = {
    {"ListBaseline", ListBaseline},
    {"ListFilters", ListFilters},
    {"SetThenGetUncompressed", SetThenGetUncompressed},
    {"SetEmptyFails", SetEmptyFails},
    {"SetWithOutputLeavesSourceUnchanged", SetWithOutputLeavesSourceUnchanged},
    {"SetRefusesRunningExecutable", SetRefusesRunningExecutable},
#ifdef RCEDIT_HAS_ZSTD
    {"ZstdRoundTrip", ZstdRoundTrip},
#else
    {"ZstdDisabledOnSet", ZstdDisabledOnSet},
    {"ZstdDisabledOnGet", ZstdDisabledOnGet},
#endif
#ifdef RCEDIT_HAS_7Z
    {"SevenZipRoundTrip", SevenZipRoundTrip},
#else
    {"SevenZipDisabledOnSet", SevenZipDisabledOnSet},
    {"SevenZipDisabledOnGet", SevenZipDisabledOnGet},
#endif
    {"LanguageResolution", LanguageResolution},
    {"RemoveResolvesLanguage", RemoveResolvesLanguage},
    {"RemoveWithOutput", RemoveWithOutput},
    {"HexdumpFormatting", HexdumpFormatting},
    {"HexdumpOfResource", HexdumpOfResource},
};

}  // namespace

int RunOpsTests()
{
    return RunGroup("ops", kCases);
}

}  // namespace rcedit::test
```

Register `RunOpsTests` in `tests/main.cpp`, add `test_ops.cpp` and group `ops` in `tests/CMakeLists.txt`.

- [ ] **Step 2: Build to verify failure**

```powershell
cmake --build --preset minimal-MinSizeRel
```
Expected: `core/ops.h` not found.

- [ ] **Step 3: Implement `ops.h/.cpp`**

`src/core/ops.h`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "core/codec.h"
#include "core/engine.h"
#include "core/resource_id.h"

namespace rcedit {

struct ListOptions
{
    std::optional<ResourceId> type;
    std::optional<ResourceId> name;
};

struct ListEntry
{
    ResourceKey key;  // lang always set
    uint32_t size;
    CodecId codec;                        // detected by magic, None if plain
    std::optional<uint64_t> contentSize;  // decompressed size when the codec is available and records it
};

[[nodiscard]] std::error_code
List(ResourceEngine& engine, const std::filesystem::path& pe, const ListOptions& options, std::vector<ListEntry>& out);

// Decompresses by magic unless 'raw'. A detected but disabled codec is errc::codec_disabled.
[[nodiscard]] std::error_code
Get(ResourceEngine& engine, const std::filesystem::path& pe, const ResourceKey& key, bool raw, std::vector<uint8_t>& out);

// key.lang unset means neutral. With 'output', 'pe' is copied there first and only the copy is modified.
[[nodiscard]] std::error_code Set(
    ResourceEngine& engine,
    const std::filesystem::path& pe,
    const ResourceKey& key,
    std::span<const uint8_t> data,
    CodecId codec,
    const std::optional<std::filesystem::path>& output);

[[nodiscard]] std::error_code Remove(
    ResourceEngine& engine,
    const std::filesystem::path& pe,
    const ResourceKey& key,
    const std::optional<std::filesystem::path>& output);

[[nodiscard]] std::error_code Hexdump(
    ResourceEngine& engine,
    const std::filesystem::path& pe,
    const ResourceKey& key,
    bool raw,
    std::optional<size_t> limit,
    std::wstring& out);

// Language resolution on an engine that is already open and readable.
// Fills 'candidates' on errc::ambiguous_language.
[[nodiscard]] std::error_code ResolveLanguage(
    ResourceEngine& engine, const ResourceKey& key, ResourceKey& resolved, std::vector<uint16_t>& candidates);

[[nodiscard]] std::wstring FormatHexdump(std::span<const uint8_t> data, std::optional<size_t> limit);

[[nodiscard]] bool IsRunningExecutable(const std::filesystem::path& path);

}  // namespace rcedit
```

`src/core/ops.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/ops.h"

#include <algorithm>
#include <format>

#include <windows.h>

#include "core/error.h"
#include "core/format.h"
#include "core/log.h"

namespace rcedit {

namespace {

namespace fs = std::filesystem;

std::error_code ReadResolved(
    ResourceEngine& engine, const fs::path& pe, const ResourceKey& key, ResourceKey& resolved, std::vector<uint8_t>& stored)
{
    if (const auto ec = engine.Open(pe, OpenMode::ReadOnly))
    {
        return ec;
    }
    std::vector<uint16_t> candidates;
    if (const auto ec = ResolveLanguage(engine, key, resolved, candidates))
    {
        return ec;
    }
    return engine.Read(resolved, stored);
}

std::error_code MaybeDecompress(std::vector<uint8_t>& data, bool raw)
{
    const CodecId detected = DetectCodec(data);
    if (raw || detected == CodecId::None)
    {
        return {};
    }

    const auto codec = FindCodec(detected);
    if (!codec)
    {
        Log::Debug(L"Payload is {} compressed but that codec is not built in", CodecName(detected));
        return codec.error();
    }

    std::vector<uint8_t> unpacked;
    if (const auto ec = (*codec)->Decompress(data, unpacked))
    {
        return ec;
    }
    data = std::move(unpacked);
    return {};
}

// Copies 'pe' to 'output' when requested and returns the path to modify.
std::error_code PrepareTarget(const fs::path& pe, const std::optional<fs::path>& output, fs::path& target)
{
    target = output ? *output : pe;

    if (IsRunningExecutable(target))
    {
        return make_error_code(errc::self_update);
    }

    if (output)
    {
        std::error_code ec;
        fs::copy_file(pe, *output, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            Log::Debug(L"Failed to copy '{}' to '{}' [{}]", pe.wstring(), output->wstring(), FormatError(ec));
            return ec;
        }
    }
    return {};
}

}  // namespace

bool IsRunningExecutable(const fs::path& path)
{
    wchar_t self[MAX_PATH];
    const DWORD length = ::GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return false;
    }

    std::error_code ec;
    const bool same = fs::equivalent(path, fs::path(self), ec);
    return !ec && same;
}

std::error_code ResolveLanguage(
    ResourceEngine& engine, const ResourceKey& key, ResourceKey& resolved, std::vector<uint16_t>& candidates)
{
    candidates.clear();
    if (key.lang)
    {
        resolved = key;
        return {};
    }

    std::vector<ResourceEntry> entries;
    if (const auto ec = engine.Enumerate(entries))
    {
        return ec;
    }

    for (const auto& entry : entries)
    {
        if (entry.key.type == key.type && entry.key.name == key.name)
        {
            candidates.push_back(*entry.key.lang);
        }
    }

    if (candidates.empty())
    {
        return make_error_code(errc::resource_not_found);
    }

    resolved = key;
    if (std::ranges::find(candidates, uint16_t(0)) != candidates.end())
    {
        resolved.lang = 0;
        candidates.clear();
        return {};
    }
    if (candidates.size() == 1)
    {
        resolved.lang = candidates.front();
        candidates.clear();
        return {};
    }

    std::ranges::sort(candidates);
    return make_error_code(errc::ambiguous_language);
}

std::error_code
List(ResourceEngine& engine, const fs::path& pe, const ListOptions& options, std::vector<ListEntry>& out)
{
    if (const auto ec = engine.Open(pe, OpenMode::ReadOnly))
    {
        return ec;
    }

    std::vector<ResourceEntry> entries;
    if (const auto ec = engine.Enumerate(entries))
    {
        return ec;
    }

    std::vector<ListEntry> result;
    for (const auto& entry : entries)
    {
        if (options.type && entry.key.type != *options.type)
        {
            continue;
        }
        if (options.name && entry.key.name != *options.name)
        {
            continue;
        }

        std::vector<uint8_t> stored;
        if (const auto ec = engine.Read(entry.key, stored))
        {
            return ec;
        }

        ListEntry item{entry.key, entry.size, DetectCodec(stored), std::nullopt};
        if (item.codec != CodecId::None)
        {
            if (const auto codec = FindCodec(item.codec))
            {
                item.contentSize = (*codec)->ContentSize(stored);
            }
        }
        result.push_back(std::move(item));
    }

    out = std::move(result);
    return {};
}

std::error_code
Get(ResourceEngine& engine, const fs::path& pe, const ResourceKey& key, bool raw, std::vector<uint8_t>& out)
{
    ResourceKey resolved;
    std::vector<uint8_t> stored;
    if (const auto ec = ReadResolved(engine, pe, key, resolved, stored))
    {
        return ec;
    }
    if (const auto ec = MaybeDecompress(stored, raw))
    {
        return ec;
    }
    out = std::move(stored);
    return {};
}

std::error_code Set(
    ResourceEngine& engine,
    const fs::path& pe,
    const ResourceKey& key,
    std::span<const uint8_t> data,
    CodecId codecId,
    const std::optional<fs::path>& output)
{
    if (data.empty())
    {
        return make_error_code(errc::empty_payload);
    }

    std::vector<uint8_t> packed;
    std::span<const uint8_t> payload = data;
    if (codecId != CodecId::None)
    {
        const auto codec = FindCodec(codecId);
        if (!codec)
        {
            return codec.error();
        }
        if (const auto ec = (*codec)->Compress(data, packed))
        {
            return ec;
        }
        payload = packed;
    }

    fs::path target;
    if (const auto ec = PrepareTarget(pe, output, target))
    {
        return ec;
    }

    ResourceKey resolved = key;
    if (!resolved.lang)
    {
        resolved.lang = 0;
    }

    if (const auto ec = engine.Open(target, OpenMode::ReadWrite))
    {
        return ec;
    }
    if (const auto ec = engine.Write(resolved, payload))
    {
        engine.Discard();
        return ec;
    }
    return engine.Commit();
}

std::error_code
Remove(ResourceEngine& engine, const fs::path& pe, const ResourceKey& key, const std::optional<fs::path>& output)
{
    fs::path target;
    if (const auto ec = PrepareTarget(pe, output, target))
    {
        return ec;
    }

    if (const auto ec = engine.Open(target, OpenMode::ReadWrite))
    {
        return ec;
    }

    ResourceKey resolved;
    std::vector<uint16_t> candidates;
    if (const auto ec = ResolveLanguage(engine, key, resolved, candidates))
    {
        engine.Discard();
        return ec;
    }
    if (const auto ec = engine.Remove(resolved))
    {
        engine.Discard();
        return ec;
    }
    return engine.Commit();
}

std::error_code Hexdump(
    ResourceEngine& engine,
    const fs::path& pe,
    const ResourceKey& key,
    bool raw,
    std::optional<size_t> limit,
    std::wstring& out)
{
    std::vector<uint8_t> data;
    if (const auto ec = Get(engine, pe, key, raw, data))
    {
        return ec;
    }
    out = FormatHexdump(data, limit);
    return {};
}

std::wstring FormatHexdump(std::span<const uint8_t> data, std::optional<size_t> limit)
{
    if (data.empty())
    {
        return L"<empty>\n";
    }

    const size_t shown = limit ? std::min(*limit, data.size()) : data.size();
    constexpr size_t kPerLine = 16;
    constexpr size_t kHexWidth = kPerLine * 3 + 1;  // "XX " * 16 plus the mid gap

    std::wstring out;
    for (size_t offset = 0; offset < shown; offset += kPerLine)
    {
        const size_t count = std::min(kPerLine, shown - offset);

        std::wstring hex;
        for (size_t i = 0; i < count; ++i)
        {
            hex += std::format(L"{:02X} ", data[offset + i]);
            if (i == 7)
            {
                hex += L' ';
            }
        }
        hex.resize(kHexWidth, L' ');

        std::wstring ascii;
        for (size_t i = 0; i < count; ++i)
        {
            const uint8_t c = data[offset + i];
            ascii += (c >= 0x20 && c <= 0x7E) ? static_cast<wchar_t>(c) : L'.';
        }

        out += std::format(L"{:08X}  {} |{}|\n", offset, hex, ascii);
    }

    if (shown < data.size())
    {
        out += std::format(L"... ({} more bytes)\n", data.size() - shown);
    }
    return out;
}

}  // namespace rcedit
```

Add `ops.h ops.cpp` to `RCEDIT_CORE_SOURCES`.

- [ ] **Step 4: Build and run tests on `minimal` and `default`**

```powershell
cmake --build --preset minimal-MinSizeRel
ctest --preset minimal-MinSizeRel
cmake --build --preset default-MinSizeRel
ctest --preset default-MinSizeRel
```
Expected: `ops` passes on both. If `HexdumpFormatting` fails on the first line, count the padding: after the 7 hex pairs there must be spaces up to column 49 of the hex area, then one space, then `|fixture|`.

- [ ] **Step 5: Commit**

```powershell
clang-format -i src/core/*.cpp src/core/*.h tests/*.cpp
git add -A
git -c user.name=fabienfl -c user.email=fabien.fl-orc@ssi.gouv.fr commit -m "Add list, get, set, remove and hexdump operations"
```

---

