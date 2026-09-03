### Task 4: Codec interface and detection

**Files:**
- Create: `src/core/codec.h`, `src/core/codec.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/main.cpp`
- Test: `tests/test_codec.cpp` (complete file; the guarded round-trip cases only compile once Tasks 5 and 6 provide the codecs)

**Interfaces:**
- Produces:
  ```cpp
  enum class CodecId { None, SevenZip, Zstd };
  class Codec {
  public:
      virtual ~Codec() = default;
      [[nodiscard]] virtual CodecId Id() const noexcept = 0;
      [[nodiscard]] virtual std::wstring_view Name() const noexcept = 0;   // L"7z", L"zstd"
      [[nodiscard]] virtual std::error_code Compress(std::span<const uint8_t> in, std::vector<uint8_t>& out) = 0;
      [[nodiscard]] virtual std::error_code Decompress(std::span<const uint8_t> in, std::vector<uint8_t>& out) = 0;
      [[nodiscard]] virtual std::optional<uint64_t> ContentSize(std::span<const uint8_t> in) = 0;
  };
  CodecId DetectCodec(std::span<const uint8_t> data) noexcept;
  std::wstring_view CodecName(CodecId id) noexcept;                          // L"none", L"7z", L"zstd"
  std::expected<CodecId, std::error_code> ParseCodecName(std::wstring_view); // unknown_codec
  std::expected<Codec*, std::error_code> FindCodec(CodecId);                 // codec_disabled; None -> invalid_argument
  bool IsCodecAvailable(CodecId) noexcept;
  std::vector<std::wstring_view> AvailableCodecNames();                      // "none" first
  Codec& ZstdCodec();      // #ifdef RCEDIT_HAS_ZSTD, defined in Task 5
  Codec& SevenZipCodec();  // #ifdef RCEDIT_HAS_7Z, defined in Task 6
  ```

- [ ] **Step 1: Write failing tests**

`tests/test_codec.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"

#include <algorithm>

#include "core/codec.h"
#include "core/error.h"

namespace rcedit::test {

namespace {

std::vector<uint8_t> Pattern(size_t size)
{
    std::vector<uint8_t> v(size);
    for (size_t i = 0; i < size; ++i)
    {
        v[i] = static_cast<uint8_t>((i * 7) % 251);
    }
    return v;
}

void DetectsMagics()
{
    const uint8_t sevenZip[] = {0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C, 0x00, 0x04};
    const uint8_t zstd[] = {0x28, 0xB5, 0x2F, 0xFD, 0x20, 0x00};
    const uint8_t plain[] = {'h', 'e', 'l', 'l', 'o'};
    const uint8_t shortSevenZip[] = {0x37, 0x7A, 0xBC};

    CHECK(DetectCodec(sevenZip) == CodecId::SevenZip);
    CHECK(DetectCodec(zstd) == CodecId::Zstd);
    CHECK(DetectCodec(plain) == CodecId::None);
    CHECK(DetectCodec(shortSevenZip) == CodecId::None);
    CHECK(DetectCodec(std::span<const uint8_t>()) == CodecId::None);
}

void NamesRoundTrip()
{
    CHECK(CodecName(CodecId::None) == L"none");
    CHECK(CodecName(CodecId::SevenZip) == L"7z");
    CHECK(CodecName(CodecId::Zstd) == L"zstd");

    auto a = ParseCodecName(L"7z");
    CHECK(a.has_value() && *a == CodecId::SevenZip);
    auto b = ParseCodecName(L"ZSTD");
    CHECK(b.has_value() && *b == CodecId::Zstd);
    auto c = ParseCodecName(L"none");
    CHECK(c.has_value() && *c == CodecId::None);
    auto d = ParseCodecName(L"lz4");
    CHECK(!d.has_value() && d.error() == errc::unknown_codec);
}

void FindCodecMatchesBuildFlags()
{
    auto none = FindCodec(CodecId::None);
    CHECK(!none.has_value());

    auto z = FindCodec(CodecId::Zstd);
#ifdef RCEDIT_HAS_ZSTD
    CHECK(z.has_value() && (*z)->Id() == CodecId::Zstd && (*z)->Name() == L"zstd");
    CHECK(IsCodecAvailable(CodecId::Zstd));
#else
    CHECK(!z.has_value() && z.error() == errc::codec_disabled);
    CHECK(!IsCodecAvailable(CodecId::Zstd));
#endif

    auto s = FindCodec(CodecId::SevenZip);
#ifdef RCEDIT_HAS_7Z
    CHECK(s.has_value() && (*s)->Id() == CodecId::SevenZip && (*s)->Name() == L"7z");
    CHECK(IsCodecAvailable(CodecId::SevenZip));
#else
    CHECK(!s.has_value() && s.error() == errc::codec_disabled);
    CHECK(!IsCodecAvailable(CodecId::SevenZip));
#endif
}

void AvailableNamesListOnlyCompiledCodecs()
{
    const auto names = AvailableCodecNames();
    CHECK(!names.empty() && names.front() == L"none");
    const bool hasZstd = std::ranges::find(names, L"zstd") != names.end();
    const bool has7z = std::ranges::find(names, L"7z") != names.end();
#ifdef RCEDIT_HAS_ZSTD
    CHECK(hasZstd);
#else
    CHECK(!hasZstd);
#endif
#ifdef RCEDIT_HAS_7Z
    CHECK(has7z);
#else
    CHECK(!has7z);
#endif
}

// Shared by the zstd and 7z cases below.
void RoundTrip(Codec& codec)
{
    for (const size_t size : {size_t(1), size_t(100), size_t(1u << 20)})
    {
        const auto input = Pattern(size);
        std::vector<uint8_t> packed;
        CHECK_EC_OK(codec.Compress(input, packed));
        CHECK(!packed.empty());
        CHECK(DetectCodec(packed) == codec.Id());

        const auto content = codec.ContentSize(packed);
        CHECK(content.has_value() && *content == size);

        std::vector<uint8_t> unpacked;
        CHECK_EC_OK(codec.Decompress(packed, unpacked));
        CHECK(unpacked == input);
    }
}

void RejectsEmptyInput(Codec& codec)
{
    std::vector<uint8_t> out;
    const auto ec = codec.Compress(std::span<const uint8_t>(), out);
    CHECK(ec == errc::empty_payload);
}

void RejectsCorruptInput(Codec& codec)
{
    const auto input = Pattern(1000);
    std::vector<uint8_t> packed;
    CHECK_EC_OK(codec.Compress(input, packed));
    // Keep the magic, trash the rest.
    for (size_t i = 8; i < packed.size(); ++i)
    {
        packed[i] = static_cast<uint8_t>(~packed[i]);
    }
    std::vector<uint8_t> out;
    const auto ec = codec.Decompress(packed, out);
    CHECK(static_cast<bool>(ec));
}

#ifdef RCEDIT_HAS_ZSTD
void ZstdRoundTrip()
{
    RoundTrip(ZstdCodec());
}
void ZstdRejectsEmpty()
{
    RejectsEmptyInput(ZstdCodec());
}
void ZstdRejectsCorrupt()
{
    RejectsCorruptInput(ZstdCodec());
}
#endif

#ifdef RCEDIT_HAS_7Z
void SevenZipRoundTrip()
{
    RoundTrip(SevenZipCodec());
}
void SevenZipRejectsEmpty()
{
    RejectsEmptyInput(SevenZipCodec());
}
void SevenZipRejectsCorrupt()
{
    RejectsCorruptInput(SevenZipCodec());
}
#endif

constexpr TestCase kCases[] = {
    {"DetectsMagics", DetectsMagics},
    {"NamesRoundTrip", NamesRoundTrip},
    {"FindCodecMatchesBuildFlags", FindCodecMatchesBuildFlags},
    {"AvailableNamesListOnlyCompiledCodecs", AvailableNamesListOnlyCompiledCodecs},
#ifdef RCEDIT_HAS_ZSTD
    {"ZstdRoundTrip", ZstdRoundTrip},
    {"ZstdRejectsEmpty", ZstdRejectsEmpty},
    {"ZstdRejectsCorrupt", ZstdRejectsCorrupt},
#endif
#ifdef RCEDIT_HAS_7Z
    {"SevenZipRoundTrip", SevenZipRoundTrip},
    {"SevenZipRejectsEmpty", SevenZipRejectsEmpty},
    {"SevenZipRejectsCorrupt", SevenZipRejectsCorrupt},
#endif
};

}  // namespace

int RunCodecTests()
{
    return RunGroup("codec", kCases);
}

}  // namespace rcedit::test
```

Register `RunCodecTests` in `tests/main.cpp` and `test_codec.cpp` plus group `codec` in `tests/CMakeLists.txt`.

- [ ] **Step 2: Build the `minimal` preset to verify failure**

```powershell
cmake --build --preset minimal-MinSizeRel
```
Expected: `core/codec.h` not found.

- [ ] **Step 3: Implement `codec.h/.cpp`**

`src/core/codec.h`:
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
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace rcedit {

enum class CodecId
{
    None,
    SevenZip,
    Zstd
};

class Codec
{
public:
    virtual ~Codec() = default;

    [[nodiscard]] virtual CodecId Id() const noexcept = 0;
    [[nodiscard]] virtual std::wstring_view Name() const noexcept = 0;

    // Empty input is errc::empty_payload.
    [[nodiscard]] virtual std::error_code Compress(std::span<const uint8_t> input, std::vector<uint8_t>& output) = 0;
    [[nodiscard]] virtual std::error_code Decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output) = 0;

    // Decompressed size recorded in the container, if any.
    [[nodiscard]] virtual std::optional<uint64_t> ContentSize(std::span<const uint8_t> input) = 0;
};

// Magic-byte detection. Always compiled, regardless of enabled codecs.
[[nodiscard]] CodecId DetectCodec(std::span<const uint8_t> data) noexcept;

[[nodiscard]] std::wstring_view CodecName(CodecId id) noexcept;
[[nodiscard]] std::expected<CodecId, std::error_code> ParseCodecName(std::wstring_view name);

// errc::codec_disabled for a codec compiled out; std::errc::invalid_argument for None.
[[nodiscard]] std::expected<Codec*, std::error_code> FindCodec(CodecId id);
[[nodiscard]] bool IsCodecAvailable(CodecId id) noexcept;

// "none" plus every codec compiled in, for help text.
[[nodiscard]] std::vector<std::wstring_view> AvailableCodecNames();

#ifdef RCEDIT_HAS_ZSTD
[[nodiscard]] Codec& ZstdCodec();
#endif
#ifdef RCEDIT_HAS_7Z
[[nodiscard]] Codec& SevenZipCodec();
#endif

}  // namespace rcedit
```

`src/core/codec.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/codec.h"

#include <algorithm>
#include <array>
#include <cwctype>

#include "core/error.h"

namespace rcedit {

namespace {

constexpr std::array<uint8_t, 6> k7zMagic = {0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C};
constexpr std::array<uint8_t, 4> kZstdMagic = {0x28, 0xB5, 0x2F, 0xFD};

template <size_t N>
bool StartsWith(std::span<const uint8_t> data, const std::array<uint8_t, N>& magic)
{
    return data.size() >= N && std::equal(magic.begin(), magic.end(), data.begin());
}

bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b)
{
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) {
               return std::towlower(x) == std::towlower(y);
           });
}

}  // namespace

CodecId DetectCodec(std::span<const uint8_t> data) noexcept
{
    if (StartsWith(data, k7zMagic))
    {
        return CodecId::SevenZip;
    }
    if (StartsWith(data, kZstdMagic))
    {
        return CodecId::Zstd;
    }
    return CodecId::None;
}

std::wstring_view CodecName(CodecId id) noexcept
{
    switch (id)
    {
        case CodecId::SevenZip:
            return L"7z";
        case CodecId::Zstd:
            return L"zstd";
        case CodecId::None:
            break;
    }
    return L"none";
}

std::expected<CodecId, std::error_code> ParseCodecName(std::wstring_view name)
{
    for (const CodecId id : {CodecId::None, CodecId::SevenZip, CodecId::Zstd})
    {
        if (EqualsIgnoreCase(name, CodecName(id)))
        {
            return id;
        }
    }
    return std::unexpected(make_error_code(errc::unknown_codec));
}

bool IsCodecAvailable(CodecId id) noexcept
{
    switch (id)
    {
        case CodecId::SevenZip:
#ifdef RCEDIT_HAS_7Z
            return true;
#else
            return false;
#endif
        case CodecId::Zstd:
#ifdef RCEDIT_HAS_ZSTD
            return true;
#else
            return false;
#endif
        case CodecId::None:
            break;
    }
    return false;
}

std::expected<Codec*, std::error_code> FindCodec(CodecId id)
{
    switch (id)
    {
        case CodecId::SevenZip:
#ifdef RCEDIT_HAS_7Z
            return &SevenZipCodec();
#else
            return std::unexpected(make_error_code(errc::codec_disabled));
#endif
        case CodecId::Zstd:
#ifdef RCEDIT_HAS_ZSTD
            return &ZstdCodec();
#else
            return std::unexpected(make_error_code(errc::codec_disabled));
#endif
        case CodecId::None:
            break;
    }
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
}

std::vector<std::wstring_view> AvailableCodecNames()
{
    std::vector<std::wstring_view> names = {CodecName(CodecId::None)};
    for (const CodecId id : {CodecId::SevenZip, CodecId::Zstd})
    {
        if (IsCodecAvailable(id))
        {
            names.push_back(CodecName(id));
        }
    }
    return names;
}

}  // namespace rcedit
```

Add `codec.h codec.cpp` to `RCEDIT_CORE_SOURCES`.

- [ ] **Step 4: Build `minimal` and run tests**

```powershell
cmake --build --preset minimal-MinSizeRel
ctest --preset minimal-MinSizeRel
```
Expected: `codec` group passes with only the four detection/lookup cases.

- [ ] **Step 5: Commit**

```powershell
clang-format -i src/core/*.cpp src/core/*.h tests/*.cpp
git add -A
git -c user.name=fabienfl -c user.email=fabien.fl-orc@ssi.gouv.fr commit -m "Add codec interface, magic detection and lookup"
```

---

