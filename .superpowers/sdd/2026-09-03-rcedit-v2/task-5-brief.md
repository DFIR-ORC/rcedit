### Task 5: zstd codec

**Files:**
- Create: `src/core/codec_zstd.h`, `src/core/codec_zstd.cpp`
- Modify: `src/core/CMakeLists.txt`
- Test: `tests/test_codec.cpp` (the `Zstd*` cases written in Task 4)

**Interfaces:**
- Consumes: `Codec`, `CodecId`, `errc`, `Log`, `AnsiToUtf16`.
- Produces: `Codec& rcedit::ZstdCodec()`.

- [ ] **Step 1: Configure and build the `no-7z` preset to verify the tests fail to link**

```powershell
cmake --preset no-7z
cmake --build --preset no-7z-MinSizeRel
```
Expected: unresolved external `rcedit::ZstdCodec`.

- [ ] **Step 2: Implement the codec**

`src/core/codec_zstd.h`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "core/codec.h"

namespace rcedit {

class ZstdCodecImpl final : public Codec
{
public:
    [[nodiscard]] CodecId Id() const noexcept override { return CodecId::Zstd; }
    [[nodiscard]] std::wstring_view Name() const noexcept override { return L"zstd"; }
    [[nodiscard]] std::error_code Compress(std::span<const uint8_t> input, std::vector<uint8_t>& output) override;
    [[nodiscard]] std::error_code Decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output) override;
    [[nodiscard]] std::optional<uint64_t> ContentSize(std::span<const uint8_t> input) override;
};

}  // namespace rcedit
```

`src/core/codec_zstd.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/codec_zstd.h"

#include <memory>
#include <new>

#include <zstd.h>

#include "core/encoding.h"
#include "core/error.h"
#include "core/log.h"

namespace rcedit {

namespace {

struct CCtxDeleter
{
    void operator()(ZSTD_CCtx* p) const noexcept { ZSTD_freeCCtx(p); }
};
struct DCtxDeleter
{
    void operator()(ZSTD_DCtx* p) const noexcept { ZSTD_freeDCtx(p); }
};

std::wstring ZstdErrorName(size_t code)
{
    return AnsiToUtf16(ZSTD_getErrorName(code));
}

}  // namespace

std::error_code ZstdCodecImpl::Compress(std::span<const uint8_t> input, std::vector<uint8_t>& output)
{
    if (input.empty())
    {
        return make_error_code(errc::empty_payload);
    }

    std::unique_ptr<ZSTD_CCtx, CCtxDeleter> cctx(ZSTD_createCCtx());
    if (!cctx)
    {
        return std::make_error_code(std::errc::not_enough_memory);
    }

    const size_t levelResult = ZSTD_CCtx_setParameter(cctx.get(), ZSTD_c_compressionLevel, 19);
    if (ZSTD_isError(levelResult))
    {
        Log::Debug(L"Failed ZSTD_CCtx_setParameter level [{}]", ZstdErrorName(levelResult));
        return std::make_error_code(std::errc::invalid_argument);
    }
    const size_t checksumResult = ZSTD_CCtx_setParameter(cctx.get(), ZSTD_c_checksumFlag, 1);
    if (ZSTD_isError(checksumResult))
    {
        Log::Debug(L"Failed ZSTD_CCtx_setParameter checksum [{}]", ZstdErrorName(checksumResult));
        return std::make_error_code(std::errc::invalid_argument);
    }

    const size_t bound = ZSTD_compressBound(input.size());
    if (ZSTD_isError(bound))
    {
        return std::make_error_code(std::errc::value_too_large);
    }

    try
    {
        output.resize(bound);
    }
    catch (const std::bad_alloc&)
    {
        return std::make_error_code(std::errc::not_enough_memory);
    }

    const size_t written = ZSTD_compress2(cctx.get(), output.data(), output.size(), input.data(), input.size());
    if (ZSTD_isError(written))
    {
        Log::Debug(L"Failed ZSTD_compress2 [{}]", ZstdErrorName(written));
        output.clear();
        return std::make_error_code(std::errc::io_error);
    }

    output.resize(written);
    return {};
}

std::optional<uint64_t> ZstdCodecImpl::ContentSize(std::span<const uint8_t> input)
{
    const unsigned long long size = ZSTD_getFrameContentSize(input.data(), input.size());
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN)
    {
        return std::nullopt;
    }
    return static_cast<uint64_t>(size);
}

std::error_code ZstdCodecImpl::Decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output)
{
    if (input.empty())
    {
        return make_error_code(errc::empty_payload);
    }

    std::unique_ptr<ZSTD_DCtx, DCtxDeleter> dctx(ZSTD_createDCtx());
    if (!dctx)
    {
        return std::make_error_code(std::errc::not_enough_memory);
    }

    std::vector<uint8_t> result;

    if (const auto known = ContentSize(input))
    {
        try
        {
            result.resize(static_cast<size_t>(*known));
        }
        catch (const std::bad_alloc&)
        {
            return std::make_error_code(std::errc::not_enough_memory);
        }

        const size_t actual = ZSTD_decompressDCtx(dctx.get(), result.data(), result.size(), input.data(), input.size());
        if (ZSTD_isError(actual))
        {
            Log::Debug(L"Failed ZSTD_decompressDCtx [{}]", ZstdErrorName(actual));
            return make_error_code(errc::corrupt_payload);
        }
        result.resize(actual);
        output = std::move(result);
        return {};
    }

    // Frame without a recorded content size: streaming loop.
    ZSTD_inBuffer in{input.data(), input.size(), 0};
    std::vector<uint8_t> chunk(ZSTD_DStreamOutSize());
    while (in.pos < in.size)
    {
        ZSTD_outBuffer out{chunk.data(), chunk.size(), 0};
        const size_t rc = ZSTD_decompressStream(dctx.get(), &out, &in);
        if (ZSTD_isError(rc))
        {
            Log::Debug(L"Failed ZSTD_decompressStream [{}]", ZstdErrorName(rc));
            return make_error_code(errc::corrupt_payload);
        }
        try
        {
            result.insert(result.end(), chunk.begin(), chunk.begin() + static_cast<ptrdiff_t>(out.pos));
        }
        catch (const std::bad_alloc&)
        {
            return std::make_error_code(std::errc::not_enough_memory);
        }
        if (rc == 0)
        {
            break;
        }
    }

    output = std::move(result);
    return {};
}

Codec& ZstdCodec()
{
    static ZstdCodecImpl codec;
    return codec;
}

}  // namespace rcedit
```

In `src/core/CMakeLists.txt`, after `add_library`:
```cmake
if(RCEDIT_ENABLE_ZSTD)
    find_package(zstd CONFIG REQUIRED)
    target_sources(rcedit_core PRIVATE codec_zstd.h codec_zstd.cpp)
    target_link_libraries(rcedit_core PRIVATE zstd::libzstd)
endif()
```
If `zstd::libzstd` is not defined by the installed port, use `zstd::libzstd_static`; check `build/no-7z/vcpkg_installed/x64-windows-static/share/zstd/zstdTargets.cmake` for the exported name.

- [ ] **Step 3: Build and run tests on `no-7z`**

```powershell
cmake --build --preset no-7z-MinSizeRel
ctest --preset no-7z-MinSizeRel
```
Expected: `codec` passes including `ZstdRoundTrip`, `ZstdRejectsEmpty`, `ZstdRejectsCorrupt`. `imports` still only `KERNEL32.dll`.

- [ ] **Step 4: Commit**

```powershell
clang-format -i src/core/*.cpp src/core/*.h
git add -A
git -c user.name=fabienfl -c user.email=fabien.fl-orc@ssi.gouv.fr commit -m "Add zstd codec"
```

---

