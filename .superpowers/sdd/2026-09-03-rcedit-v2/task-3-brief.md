### Task 3: Resource identifiers

**Files:**
- Create: `src/core/resource_id.h`, `src/core/resource_id.cpp`
- Modify: `src/core/CMakeLists.txt`, `src/core/format.h` (add `std::formatter<ResourceId, wchar_t>`), `tests/CMakeLists.txt`, `tests/main.cpp`
- Test: `tests/test_resource_id.cpp`

**Interfaces:**
- Produces:
  ```cpp
  using ResourceId = std::variant<uint16_t, std::wstring>;
  struct ResourceKey { ResourceId type; ResourceId name; std::optional<uint16_t> lang; };
  std::expected<ResourceId, std::error_code> ParseResourceName(std::wstring_view);  // "#n" or string
  std::expected<ResourceId, std::error_code> ParseResourceType(std::wstring_view);  // aliases, "#n" or string
  std::expected<uint16_t, std::error_code>   ParseLang(std::wstring_view);          // decimal or 0x hex
  std::wstring FormatResourceName(const ResourceId&);   // "#n" or string
  std::wstring FormatResourceType(const ResourceId&);   // "RT_RCDATA", "#n" or string
  bool operator==(const ResourceKey&, const ResourceKey&);  // defaulted
  const wchar_t* ToLpcwstr(const ResourceId&);           // MAKEINTRESOURCEW or c_str
  ResourceId FromLpcwstr(const wchar_t*);                // IS_INTRESOURCE aware, copies string
  ```

- [ ] **Step 1: Write failing tests**

`tests/test_resource_id.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "check.h"

#include <windows.h>

#include "core/error.h"
#include "core/format.h"
#include "core/resource_id.h"

namespace rcedit::test {

namespace {

bool IsNumeric(const ResourceId& id, uint16_t expected)
{
    return std::holds_alternative<uint16_t>(id) && std::get<uint16_t>(id) == expected;
}

bool IsString(const ResourceId& id, std::wstring_view expected)
{
    return std::holds_alternative<std::wstring>(id) && std::get<std::wstring>(id) == expected;
}

void ParsesHashNumeric()
{
    auto r = ParseResourceName(L"#101");
    CHECK(r.has_value() && IsNumeric(*r, 101));
    auto t = ParseResourceType(L"#10");
    CHECK(t.has_value() && IsNumeric(*t, 10));
}

void BareDecimalIsAString()
{
    auto r = ParseResourceName(L"101");
    CHECK(r.has_value() && IsString(*r, L"101"));
}

void RejectsBadHashForms()
{
    CHECK(!ParseResourceName(L"#").has_value());
    CHECK(!ParseResourceName(L"#0").has_value());
    CHECK(!ParseResourceName(L"#65536").has_value());
    CHECK(!ParseResourceName(L"#12a").has_value());
    CHECK(!ParseResourceName(L"#-1").has_value());
    CHECK(ParseResourceName(L"#").error() == errc::invalid_identifier);
}

void RejectsEmpty()
{
    CHECK(!ParseResourceName(L"").has_value());
    CHECK(!ParseResourceType(L"").has_value());
}

void TypeAliasesAreCaseInsensitiveWithOrWithoutPrefix()
{
    auto a = ParseResourceType(L"RT_RCDATA");
    auto b = ParseResourceType(L"rcdata");
    auto c = ParseResourceType(L"Rt_RcData");
    CHECK(a.has_value() && IsNumeric(*a, 10));
    CHECK(b.has_value() && IsNumeric(*b, 10));
    CHECK(c.has_value() && IsNumeric(*c, 10));

    auto m = ParseResourceType(L"MANIFEST");
    CHECK(m.has_value() && IsNumeric(*m, 24));
    auto gi = ParseResourceType(L"GROUP_ICON");
    CHECK(gi.has_value() && IsNumeric(*gi, 14));
}

void UnknownTypeIsAString()
{
    auto r = ParseResourceType(L"MYTYPE");
    CHECK(r.has_value() && IsString(*r, L"MYTYPE"));
}

void NameNeverUsesAliases()
{
    auto r = ParseResourceName(L"RCDATA");
    CHECK(r.has_value() && IsString(*r, L"RCDATA"));
}

void FormatsTypesWithAliasOrHash()
{
    CHECK(FormatResourceType(ResourceId(uint16_t(10))) == L"RT_RCDATA");
    CHECK(FormatResourceType(ResourceId(uint16_t(24))) == L"RT_MANIFEST");
    CHECK(FormatResourceType(ResourceId(uint16_t(240))) == L"#240");
    CHECK(FormatResourceType(ResourceId(std::wstring(L"MYTYPE"))) == L"MYTYPE");
}

void FormatsNamesWithHashOrString()
{
    CHECK(FormatResourceName(ResourceId(uint16_t(101))) == L"#101");
    CHECK(FormatResourceName(ResourceId(std::wstring(L"CONFIG"))) == L"CONFIG");
}

void FormatRoundTrips()
{
    for (const wchar_t* s : {L"#1", L"#65535", L"CONFIG", L"101"})
    {
        auto id = ParseResourceName(s);
        CHECK(id.has_value());
        CHECK(FormatResourceName(*id) == s);
    }
    for (const wchar_t* s : {L"RT_ICON", L"RT_VERSION", L"#200", L"MYTYPE"})
    {
        auto id = ParseResourceType(s);
        CHECK(id.has_value());
        CHECK(FormatResourceType(*id) == s);
    }
}

void ParsesLang()
{
    auto a = ParseLang(L"1033");
    CHECK(a.has_value() && *a == 1033);
    auto b = ParseLang(L"0x409");
    CHECK(b.has_value() && *b == 0x409);
    auto c = ParseLang(L"0");
    CHECK(c.has_value() && *c == 0);
    CHECK(!ParseLang(L"").has_value());
    CHECK(!ParseLang(L"65536").has_value());
    CHECK(!ParseLang(L"-1").has_value());
    CHECK(!ParseLang(L"12x").has_value());
    CHECK(ParseLang(L"x").error() == errc::invalid_language);
}

void Win32PointerConversion()
{
    const ResourceId numeric(uint16_t(10));
    CHECK(ToLpcwstr(numeric) == MAKEINTRESOURCEW(10));

    const ResourceId text(std::wstring(L"CONFIG"));
    CHECK(std::wstring_view(ToLpcwstr(text)) == L"CONFIG");

    CHECK(IsNumeric(FromLpcwstr(MAKEINTRESOURCEW(16)), 16));
    CHECK(IsString(FromLpcwstr(L"NAME"), L"NAME"));
}

void KeyEquality()
{
    const ResourceKey a{ResourceId(uint16_t(10)), ResourceId(std::wstring(L"X")), 0};
    const ResourceKey b{ResourceId(uint16_t(10)), ResourceId(std::wstring(L"X")), 0};
    const ResourceKey c{ResourceId(uint16_t(10)), ResourceId(std::wstring(L"X")), std::nullopt};
    CHECK(a == b);
    CHECK(!(a == c));
}

void FormatterWorks()
{
    const ResourceId id(std::wstring(L"CONFIG"));
    CHECK(std::format(L"{}", id) == L"CONFIG");
    const ResourceId n(uint16_t(7));
    CHECK(std::format(L"{}", n) == L"#7");
}

constexpr TestCase kCases[] = {
    {"ParsesHashNumeric", ParsesHashNumeric},
    {"BareDecimalIsAString", BareDecimalIsAString},
    {"RejectsBadHashForms", RejectsBadHashForms},
    {"RejectsEmpty", RejectsEmpty},
    {"TypeAliasesAreCaseInsensitiveWithOrWithoutPrefix", TypeAliasesAreCaseInsensitiveWithOrWithoutPrefix},
    {"UnknownTypeIsAString", UnknownTypeIsAString},
    {"NameNeverUsesAliases", NameNeverUsesAliases},
    {"FormatsTypesWithAliasOrHash", FormatsTypesWithAliasOrHash},
    {"FormatsNamesWithHashOrString", FormatsNamesWithHashOrString},
    {"FormatRoundTrips", FormatRoundTrips},
    {"ParsesLang", ParsesLang},
    {"Win32PointerConversion", Win32PointerConversion},
    {"KeyEquality", KeyEquality},
    {"FormatterWorks", FormatterWorks},
};

}  // namespace

int RunResourceIdTests()
{
    return RunGroup("resource_id", kCases);
}

}  // namespace rcedit::test
```

Register `RunResourceIdTests` in `tests/main.cpp` and `test_resource_id.cpp` plus group `resource_id` in `tests/CMakeLists.txt`.

- [ ] **Step 2: Build to verify the tests fail**

```powershell
cmake --build --preset minimal-MinSizeRel
```
Expected: `core/resource_id.h` not found.

- [ ] **Step 3: Implement `resource_id.h/.cpp` and the formatter**

`src/core/resource_id.h`:
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
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

namespace rcedit {

// Numeric id (MAKEINTRESOURCE) or string name.
using ResourceId = std::variant<uint16_t, std::wstring>;

struct ResourceKey
{
    ResourceId type;
    ResourceId name;
    std::optional<uint16_t> lang;  // nullopt: not specified, resolved by ops

    bool operator==(const ResourceKey&) const = default;
};

// "#n" -> numeric (1..65535), anything else non-empty -> string.
[[nodiscard]] std::expected<ResourceId, std::error_code> ParseResourceName(std::wstring_view text);

// Like ParseResourceName, plus RT_* aliases (with or without "RT_", case-insensitive).
[[nodiscard]] std::expected<ResourceId, std::error_code> ParseResourceType(std::wstring_view text);

// Decimal or "0x" hex, 0..65535.
[[nodiscard]] std::expected<uint16_t, std::error_code> ParseLang(std::wstring_view text);

[[nodiscard]] std::wstring FormatResourceName(const ResourceId& id);  // "#n" or string
[[nodiscard]] std::wstring FormatResourceType(const ResourceId& id);  // "RT_X", "#n" or string

// Win32 boundary. The returned pointer aliases 'id' for string names.
[[nodiscard]] const wchar_t* ToLpcwstr(const ResourceId& id) noexcept;
[[nodiscard]] ResourceId FromLpcwstr(const wchar_t* value);

}  // namespace rcedit
```

`src/core/resource_id.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/resource_id.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <format>

#include <windows.h>

#include "core/error.h"

namespace rcedit {

namespace {

struct Alias
{
    std::wstring_view name;  // without RT_
    uint16_t id;
};

// clang-format off
constexpr std::array<Alias, 21> kAliases = {{
    {L"CURSOR", 1},       {L"BITMAP", 2},       {L"ICON", 3},          {L"MENU", 4},
    {L"DIALOG", 5},       {L"STRING", 6},       {L"FONTDIR", 7},       {L"FONT", 8},
    {L"ACCELERATOR", 9},  {L"RCDATA", 10},      {L"MESSAGETABLE", 11}, {L"GROUP_CURSOR", 12},
    {L"GROUP_ICON", 14},  {L"VERSION", 16},     {L"DLGINCLUDE", 17},   {L"PLUGPLAY", 19},
    {L"VXD", 20},         {L"ANICURSOR", 21},   {L"ANIICON", 22},      {L"HTML", 23},
    {L"MANIFEST", 24},
}};
// clang-format on

bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b)
{
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) {
               return std::towupper(x) == std::towupper(y);
           });
}

std::optional<uint16_t> ParseUnsigned(std::wstring_view digits, int base)
{
    if (digits.empty())
    {
        return std::nullopt;
    }

    uint32_t value = 0;
    for (wchar_t c : digits)
    {
        int digit = -1;
        if (c >= L'0' && c <= L'9')
        {
            digit = c - L'0';
        }
        else if (base == 16 && c >= L'a' && c <= L'f')
        {
            digit = 10 + (c - L'a');
        }
        else if (base == 16 && c >= L'A' && c <= L'F')
        {
            digit = 10 + (c - L'A');
        }
        if (digit < 0 || digit >= base)
        {
            return std::nullopt;
        }
        value = value * static_cast<uint32_t>(base) + static_cast<uint32_t>(digit);
        if (value > 0xFFFF)
        {
            return std::nullopt;
        }
    }
    return static_cast<uint16_t>(value);
}

std::optional<uint16_t> AliasToId(std::wstring_view text)
{
    if (text.size() > 3 && EqualsIgnoreCase(text.substr(0, 3), L"RT_"))
    {
        text.remove_prefix(3);
    }
    for (const auto& alias : kAliases)
    {
        if (EqualsIgnoreCase(alias.name, text))
        {
            return alias.id;
        }
    }
    return std::nullopt;
}

std::optional<std::wstring_view> IdToAlias(uint16_t id)
{
    for (const auto& alias : kAliases)
    {
        if (alias.id == id)
        {
            return alias.name;
        }
    }
    return std::nullopt;
}

}  // namespace

std::expected<ResourceId, std::error_code> ParseResourceName(std::wstring_view text)
{
    if (text.empty())
    {
        return std::unexpected(make_error_code(errc::invalid_identifier));
    }

    if (text.front() == L'#')
    {
        const auto id = ParseUnsigned(text.substr(1), 10);
        if (!id || *id == 0)
        {
            return std::unexpected(make_error_code(errc::invalid_identifier));
        }
        return ResourceId(*id);
    }

    return ResourceId(std::wstring(text));
}

std::expected<ResourceId, std::error_code> ParseResourceType(std::wstring_view text)
{
    if (const auto id = AliasToId(text))
    {
        return ResourceId(*id);
    }
    return ParseResourceName(text);
}

std::expected<uint16_t, std::error_code> ParseLang(std::wstring_view text)
{
    std::optional<uint16_t> value;
    if (text.size() > 2 && text[0] == L'0' && (text[1] == L'x' || text[1] == L'X'))
    {
        value = ParseUnsigned(text.substr(2), 16);
    }
    else
    {
        value = ParseUnsigned(text, 10);
    }

    if (!value)
    {
        return std::unexpected(make_error_code(errc::invalid_language));
    }
    return *value;
}

std::wstring FormatResourceName(const ResourceId& id)
{
    if (const auto* n = std::get_if<uint16_t>(&id))
    {
        return std::format(L"#{}", *n);
    }
    return std::get<std::wstring>(id);
}

std::wstring FormatResourceType(const ResourceId& id)
{
    if (const auto* n = std::get_if<uint16_t>(&id))
    {
        if (const auto alias = IdToAlias(*n))
        {
            return std::format(L"RT_{}", *alias);
        }
        return std::format(L"#{}", *n);
    }
    return std::get<std::wstring>(id);
}

const wchar_t* ToLpcwstr(const ResourceId& id) noexcept
{
    if (const auto* n = std::get_if<uint16_t>(&id))
    {
        return MAKEINTRESOURCEW(*n);
    }
    return std::get<std::wstring>(id).c_str();
}

ResourceId FromLpcwstr(const wchar_t* value)
{
    if (IS_INTRESOURCE(value))
    {
        return ResourceId(static_cast<uint16_t>(reinterpret_cast<uintptr_t>(value) & 0xFFFF));
    }
    return ResourceId(std::wstring(value));
}

}  // namespace rcedit
```

Add to `src/core/format.h`: include `core/resource_id.h` at the top, and after the `rcedit` namespace add:
```cpp
template <>
struct std::formatter<rcedit::ResourceId, wchar_t>
{
    constexpr auto parse(std::wformat_parse_context& ctx) { return ctx.begin(); }

    auto format(const rcedit::ResourceId& id, std::wformat_context& ctx) const
    {
        return std::format_to(ctx.out(), L"{}", rcedit::FormatResourceName(id));
    }
};
```

Add `resource_id.h resource_id.cpp` to `RCEDIT_CORE_SOURCES`.

- [ ] **Step 4: Build and run the tests**

```powershell
cmake --build --preset minimal-MinSizeRel
ctest --preset minimal-MinSizeRel
```
Expected: all groups pass including `resource_id`.

- [ ] **Step 5: Commit**

```powershell
clang-format -i src/core/*.cpp src/core/*.h tests/*.cpp
git add -A
git -c user.name=fabienfl -c user.email=fabien.fl-orc@ssi.gouv.fr commit -m "Add resource identifier parsing and formatting"
```

---

