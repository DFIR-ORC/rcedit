### Task 6: 7z codec

**Files:**
- Create: `src/core/sevenzip/sevenzip.h`, `src/core/sevenzip/in_mem_stream.h`, `src/core/sevenzip/in_mem_stream.cpp`, `src/core/sevenzip/out_mem_stream.h`, `src/core/sevenzip/out_mem_stream.cpp`, `src/core/sevenzip/update_callback.h`, `src/core/sevenzip/update_callback.cpp`, `src/core/sevenzip/extract_callback.h`, `src/core/sevenzip/extract_callback.cpp`, `src/core/codec_7z.h`, `src/core/codec_7z.cpp`
- Modify: `src/core/CMakeLists.txt`
- Test: `tests/test_codec.cpp` (the `SevenZip*` cases written in Task 4)

**Interfaces:**
- Consumes: `Codec`, `CodecId`, `errc`, `HResultError`, `Log`.
- Produces: `Codec& rcedit::SevenZipCodec()`. Internal: `rcedit::sevenzip::EnsureInitialized()`, `InMemStream(std::span<const uint8_t>)`, `OutMemStream(std::vector<uint8_t>&)`, `UpdateCallback(std::span<const uint8_t>, std::wstring name)`, `ExtractCallback(std::vector<uint8_t>&)`.

Background: the overlay port applies `my-com.patch` so app classes can inherit 7-Zip's `CMyUnknownImp` and use `Z7_COM_UNKNOWN_IMP_n`. In a static build the port defines `_7ZIP_STATIC` publicly and format/codec registrars must be called by hand (see the comment in `external/vcpkg_overlay_ports/7zip/7zip.h`).

- [ ] **Step 1: Configure and build the `no-zstd` preset to verify the tests fail to link**

```powershell
cmake --preset no-zstd
cmake --build --preset no-zstd-MinSizeRel
```
Expected: unresolved external `rcedit::SevenZipCodec`.

- [ ] **Step 2: Write the 7-Zip include wrapper and the two streams**

`src/core/sevenzip/sevenzip.h`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

// 7-Zip headers are not /W4 clean.
#pragma warning(push, 0)
#include <7zip/7zip.h>
#include <7zip/extras.h>
#pragma warning(pop)

namespace rcedit::sevenzip {

// Static 7-Zip needs explicit registration of formats and codecs. Idempotent.
inline void EnsureInitialized()
{
#ifdef _7ZIP_STATIC
    static const bool initialized = [] {
        ::lib7zCrcTableInit();
        NArchive::N7z::Register();
        NCompress::RegisterCodecCopy();
        NCompress::NBcj::RegisterCodecBCJ();
        NCompress::NBcj2::RegisterCodecBCJ2();
        NCompress::NLzma::RegisterCodecLZMA();
        NCompress::NLzma2::RegisterCodecLZMA2();
        return true;
    }();
    (void)initialized;
#endif
}

}  // namespace rcedit::sevenzip
```

`src/core/sevenzip/in_mem_stream.h`:
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
#include <span>

#include "core/sevenzip/sevenzip.h"

namespace rcedit::sevenzip {

// Read-only IInStream over caller-owned memory.
class InMemStream final
    : public IInStream
    , public CMyUnknownImp
{
public:
    Z7_COM_UNKNOWN_IMP_2(ISequentialInStream, IInStream)

    explicit InMemStream(std::span<const uint8_t> buffer) noexcept;

    STDMETHOD(Read)(void* data, UInt32 size, UInt32* processedSize) override;
    STDMETHOD(Seek)(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) override;

private:
    std::span<const uint8_t> m_buffer;
    size_t m_pos = 0;
};

}  // namespace rcedit::sevenzip
```

`src/core/sevenzip/in_mem_stream.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/sevenzip/in_mem_stream.h"

#include <algorithm>
#include <cstring>

namespace rcedit::sevenzip {

InMemStream::InMemStream(std::span<const uint8_t> buffer) noexcept
    : m_buffer(buffer)
{
}

STDMETHODIMP InMemStream::Read(void* data, UInt32 size, UInt32* processedSize)
{
    if (processedSize != nullptr)
    {
        *processedSize = 0;
    }
    if (data == nullptr)
    {
        return E_POINTER;
    }
    if (size == 0 || m_pos >= m_buffer.size())
    {
        return S_OK;
    }

    const size_t available = m_buffer.size() - m_pos;
    const size_t count = std::min<size_t>(size, available);
    std::memcpy(data, m_buffer.data() + m_pos, count);
    m_pos += count;
    if (processedSize != nullptr)
    {
        *processedSize = static_cast<UInt32>(count);
    }
    return S_OK;
}

STDMETHODIMP InMemStream::Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition)
{
    Int64 base = 0;
    switch (seekOrigin)
    {
        case STREAM_SEEK_SET:
            base = 0;
            break;
        case STREAM_SEEK_CUR:
            base = static_cast<Int64>(m_pos);
            break;
        case STREAM_SEEK_END:
            base = static_cast<Int64>(m_buffer.size());
            break;
        default:
            return STG_E_INVALIDFUNCTION;
    }

    const Int64 target = base + offset;
    if (target < 0)
    {
        return HRESULT_WIN32_ERROR_NEGATIVE_SEEK;
    }

    m_pos = static_cast<size_t>(target);
    if (newPosition != nullptr)
    {
        *newPosition = static_cast<UInt64>(m_pos);
    }
    return S_OK;
}

}  // namespace rcedit::sevenzip
```

`src/core/sevenzip/out_mem_stream.h`:
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
#include <vector>

#include "core/sevenzip/sevenzip.h"

namespace rcedit::sevenzip {

// Growable IOutStream writing into a caller-owned vector.
class OutMemStream final
    : public IOutStream
    , public CMyUnknownImp
{
public:
    Z7_COM_UNKNOWN_IMP_2(ISequentialOutStream, IOutStream)

    explicit OutMemStream(std::vector<uint8_t>& buffer) noexcept;

    STDMETHOD(Write)(const void* data, UInt32 size, UInt32* processedSize) override;
    STDMETHOD(Seek)(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) override;
    STDMETHOD(SetSize)(UInt64 newSize) override;

private:
    std::vector<uint8_t>& m_buffer;
    size_t m_pos = 0;
};

}  // namespace rcedit::sevenzip
```

`src/core/sevenzip/out_mem_stream.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/sevenzip/out_mem_stream.h"

#include <cstring>
#include <new>

namespace rcedit::sevenzip {

OutMemStream::OutMemStream(std::vector<uint8_t>& buffer) noexcept
    : m_buffer(buffer)
{
}

STDMETHODIMP OutMemStream::Write(const void* data, UInt32 size, UInt32* processedSize)
{
    if (processedSize != nullptr)
    {
        *processedSize = 0;
    }
    if (data == nullptr)
    {
        return E_POINTER;
    }
    if (size == 0)
    {
        return S_OK;
    }

    const size_t required = m_pos + size;
    try
    {
        if (m_buffer.size() < required)
        {
            m_buffer.resize(required);
        }
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    std::memcpy(m_buffer.data() + m_pos, data, size);
    m_pos += size;
    if (processedSize != nullptr)
    {
        *processedSize = size;
    }
    return S_OK;
}

STDMETHODIMP OutMemStream::Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition)
{
    Int64 base = 0;
    switch (seekOrigin)
    {
        case STREAM_SEEK_SET:
            base = 0;
            break;
        case STREAM_SEEK_CUR:
            base = static_cast<Int64>(m_pos);
            break;
        case STREAM_SEEK_END:
            base = static_cast<Int64>(m_buffer.size());
            break;
        default:
            return STG_E_INVALIDFUNCTION;
    }

    const Int64 target = base + offset;
    if (target < 0)
    {
        return HRESULT_WIN32_ERROR_NEGATIVE_SEEK;
    }

    m_pos = static_cast<size_t>(target);
    if (newPosition != nullptr)
    {
        *newPosition = static_cast<UInt64>(m_pos);
    }
    return S_OK;
}

STDMETHODIMP OutMemStream::SetSize(UInt64 newSize)
{
    try
    {
        m_buffer.resize(static_cast<size_t>(newSize));
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

}  // namespace rcedit::sevenzip
```

- [ ] **Step 3: Write the update and extract callbacks**

`src/core/sevenzip/update_callback.h`:
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
#include <span>
#include <string>

#include "core/sevenzip/sevenzip.h"

namespace rcedit::sevenzip {

// Describes exactly one in-memory file to IOutArchive::UpdateItems.
class UpdateCallback final
    : public IArchiveUpdateCallback
    , public CMyUnknownImp
{
public:
    Z7_COM_UNKNOWN_IMP_1(IArchiveUpdateCallback)

    UpdateCallback(std::span<const uint8_t> content, std::wstring name);

    // IProgress
    STDMETHOD(SetTotal)(UInt64 size) override;
    STDMETHOD(SetCompleted)(const UInt64* completeValue) override;

    // IArchiveUpdateCallback
    STDMETHOD(GetUpdateItemInfo)(UInt32 index, Int32* newData, Int32* newProperties, UInt32* indexInArchive) override;
    STDMETHOD(GetProperty)(UInt32 index, PROPID propID, PROPVARIANT* value) override;
    STDMETHOD(GetStream)(UInt32 index, ISequentialInStream** inStream) override;
    STDMETHOD(SetOperationResult)(Int32 operationResult) override;

    [[nodiscard]] bool Failed() const noexcept { return m_failed; }

private:
    std::span<const uint8_t> m_content;
    std::wstring m_name;
    FILETIME m_time {};
    bool m_failed = false;
};

}  // namespace rcedit::sevenzip
```

`src/core/sevenzip/update_callback.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/sevenzip/update_callback.h"

#include "core/sevenzip/in_mem_stream.h"

namespace rcedit::sevenzip {

UpdateCallback::UpdateCallback(std::span<const uint8_t> content, std::wstring name)
    : m_content(content)
    , m_name(std::move(name))
{
    ::GetSystemTimeAsFileTime(&m_time);
}

STDMETHODIMP UpdateCallback::SetTotal(UInt64)
{
    return S_OK;
}

STDMETHODIMP UpdateCallback::SetCompleted(const UInt64*)
{
    return S_OK;
}

STDMETHODIMP UpdateCallback::GetUpdateItemInfo(UInt32, Int32* newData, Int32* newProperties, UInt32* indexInArchive)
{
    if (newData != nullptr)
    {
        *newData = 1;
    }
    if (newProperties != nullptr)
    {
        *newProperties = 1;
    }
    if (indexInArchive != nullptr)
    {
        *indexInArchive = static_cast<UInt32>(-1);
    }
    return S_OK;
}

STDMETHODIMP UpdateCallback::GetProperty(UInt32 index, PROPID propID, PROPVARIANT* value)
{
    if (index != 0)
    {
        return E_INVALIDARG;
    }

    NWindows::NCOM::CPropVariant prop;
    switch (propID)
    {
        case kpidPath:
            prop = m_name.c_str();
            break;
        case kpidIsDir:
            prop = false;
            break;
        case kpidIsAnti:
            prop = false;
            break;
        case kpidSize:
            prop = static_cast<UInt64>(m_content.size());
            break;
        case kpidAttrib:
            prop = static_cast<UInt32>(FILE_ATTRIBUTE_NORMAL);
            break;
        case kpidCTime:
        case kpidATime:
        case kpidMTime:
            prop = m_time;
            break;
        default:
            break;
    }
    prop.Detach(value);
    return S_OK;
}

STDMETHODIMP UpdateCallback::GetStream(UInt32 index, ISequentialInStream** inStream)
{
    if (inStream == nullptr)
    {
        return E_POINTER;
    }
    *inStream = nullptr;
    if (index != 0)
    {
        return E_INVALIDARG;
    }
    if (m_content.empty())
    {
        return S_OK;  // 7-Zip expects a null stream for empty files
    }

    CMyComPtr<ISequentialInStream> stream = new InMemStream(m_content);
    *inStream = stream.Detach();
    return S_OK;
}

STDMETHODIMP UpdateCallback::SetOperationResult(Int32 operationResult)
{
    if (operationResult != NArchive::NUpdate::NOperationResult::kOK)
    {
        m_failed = true;
    }
    return S_OK;
}

}  // namespace rcedit::sevenzip
```

`src/core/sevenzip/extract_callback.h`:
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
#include <vector>

#include "core/sevenzip/sevenzip.h"

namespace rcedit::sevenzip {

// Extracts item 0 into a caller-owned vector; every other index is skipped.
class ExtractCallback final
    : public IArchiveExtractCallback
    , public CMyUnknownImp
{
public:
    Z7_COM_UNKNOWN_IMP_1(IArchiveExtractCallback)

    explicit ExtractCallback(std::vector<uint8_t>& output) noexcept;

    // IProgress
    STDMETHOD(SetTotal)(UInt64 size) override;
    STDMETHOD(SetCompleted)(const UInt64* completeValue) override;

    // IArchiveExtractCallback
    STDMETHOD(GetStream)(UInt32 index, ISequentialOutStream** outStream, Int32 askExtractMode) override;
    STDMETHOD(PrepareOperation)(Int32 askExtractMode) override;
    STDMETHOD(SetOperationResult)(Int32 operationResult) override;

    [[nodiscard]] bool Failed() const noexcept { return m_failed; }

private:
    std::vector<uint8_t>& m_output;
    bool m_failed = false;
};

}  // namespace rcedit::sevenzip
```

`src/core/sevenzip/extract_callback.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/sevenzip/extract_callback.h"

#include "core/sevenzip/out_mem_stream.h"

namespace rcedit::sevenzip {

ExtractCallback::ExtractCallback(std::vector<uint8_t>& output) noexcept
    : m_output(output)
{
}

STDMETHODIMP ExtractCallback::SetTotal(UInt64)
{
    return S_OK;
}

STDMETHODIMP ExtractCallback::SetCompleted(const UInt64*)
{
    return S_OK;
}

STDMETHODIMP ExtractCallback::GetStream(UInt32 index, ISequentialOutStream** outStream, Int32 askExtractMode)
{
    if (outStream == nullptr)
    {
        return E_POINTER;
    }
    *outStream = nullptr;

    if (index != 0 || askExtractMode != NArchive::NExtract::NAskMode::kExtract)
    {
        return S_OK;
    }

    m_output.clear();
    CMyComPtr<ISequentialOutStream> stream = new OutMemStream(m_output);
    *outStream = stream.Detach();
    return S_OK;
}

STDMETHODIMP ExtractCallback::PrepareOperation(Int32)
{
    return S_OK;
}

STDMETHODIMP ExtractCallback::SetOperationResult(Int32 operationResult)
{
    if (operationResult != NArchive::NExtract::NOperationResult::kOK)
    {
        m_failed = true;
    }
    return S_OK;
}

}  // namespace rcedit::sevenzip
```

- [ ] **Step 4: Write the codec**

`src/core/codec_7z.h`:
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

// Single-entry 7z archive, LZMA2 level 9, entry named "payload".
class SevenZipCodecImpl final : public Codec
{
public:
    [[nodiscard]] CodecId Id() const noexcept override { return CodecId::SevenZip; }
    [[nodiscard]] std::wstring_view Name() const noexcept override { return L"7z"; }
    [[nodiscard]] std::error_code Compress(std::span<const uint8_t> input, std::vector<uint8_t>& output) override;
    [[nodiscard]] std::error_code Decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output) override;
    [[nodiscard]] std::optional<uint64_t> ContentSize(std::span<const uint8_t> input) override;
};

}  // namespace rcedit
```

`src/core/codec_7z.cpp`:
```cpp
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "core/codec_7z.h"

#include "core/error.h"
#include "core/format.h"
#include "core/log.h"
#include "core/sevenzip/extract_callback.h"
#include "core/sevenzip/in_mem_stream.h"
#include "core/sevenzip/out_mem_stream.h"
#include "core/sevenzip/sevenzip.h"
#include "core/sevenzip/update_callback.h"

namespace rcedit {

namespace {

constexpr const wchar_t* kEntryName = L"payload";
constexpr UInt32 kLevel = 9;

// Opens a 7z archive from memory. Returns a null pointer on failure.
CMyComPtr<IInArchive> OpenArchive(std::span<const uint8_t> input, std::error_code& ec)
{
    sevenzip::EnsureInitialized();

    CMyComPtr<IInArchive> archive;
    HRESULT hr = ::CreateObject(&CLSID_CFormat7z, &IID_IInArchive, reinterpret_cast<void**>(&archive));
    if (FAILED(hr) || !archive)
    {
        Log::Debug(L"Failed CreateObject(IInArchive) [{}]", FormatError(HResultError(hr)));
        ec = HResultError(hr);
        return nullptr;
    }

    CMyComPtr<IInStream> stream = new sevenzip::InMemStream(input);
    hr = archive->Open(stream, nullptr, nullptr);
    if (FAILED(hr) || hr == S_FALSE)
    {
        Log::Debug(L"Failed IInArchive::Open [{}]", FormatError(HResultError(hr)));
        ec = make_error_code(errc::corrupt_payload);
        return nullptr;
    }

    UInt32 count = 0;
    hr = archive->GetNumberOfItems(&count);
    if (FAILED(hr) || count != 1)
    {
        Log::Debug(L"Unexpected 7z item count: {}", count);
        ec = make_error_code(errc::corrupt_payload);
        return nullptr;
    }

    ec.clear();
    return archive;
}

}  // namespace

std::error_code SevenZipCodecImpl::Compress(std::span<const uint8_t> input, std::vector<uint8_t>& output)
{
    if (input.empty())
    {
        return make_error_code(errc::empty_payload);
    }

    sevenzip::EnsureInitialized();

    CMyComPtr<IOutArchive> archive;
    HRESULT hr = ::CreateObject(&CLSID_CFormat7z, &IID_IOutArchive, reinterpret_cast<void**>(&archive));
    if (FAILED(hr) || !archive)
    {
        Log::Debug(L"Failed CreateObject(IOutArchive) [{}]", FormatError(HResultError(hr)));
        return HResultError(hr);
    }

    CMyComPtr<ISetProperties> setProperties;
    hr = archive->QueryInterface(IID_ISetProperties, reinterpret_cast<void**>(&setProperties));
    if (FAILED(hr) || !setProperties)
    {
        Log::Debug(L"Failed QueryInterface(ISetProperties) [{}]", FormatError(HResultError(hr)));
        return HResultError(hr);
    }

    const wchar_t* names[] = {L"x"};
    NWindows::NCOM::CPropVariant values[] = {kLevel};
    hr = setProperties->SetProperties(names, values, 1);
    if (FAILED(hr))
    {
        Log::Debug(L"Failed ISetProperties::SetProperties [{}]", FormatError(HResultError(hr)));
        return HResultError(hr);
    }

    output.clear();
    CMyComPtr<IOutStream> outStream = new sevenzip::OutMemStream(output);
    sevenzip::UpdateCallback* callbackImpl = new sevenzip::UpdateCallback(input, kEntryName);
    CMyComPtr<IArchiveUpdateCallback> callback = callbackImpl;

    hr = archive->UpdateItems(outStream, 1, callback);
    if (FAILED(hr))
    {
        Log::Debug(L"Failed IOutArchive::UpdateItems [{}]", FormatError(HResultError(hr)));
        output.clear();
        return HResultError(hr);
    }
    if (callbackImpl->Failed())
    {
        output.clear();
        return std::make_error_code(std::errc::io_error);
    }

    return {};
}

std::optional<uint64_t> SevenZipCodecImpl::ContentSize(std::span<const uint8_t> input)
{
    std::error_code ec;
    CMyComPtr<IInArchive> archive = OpenArchive(input, ec);
    if (ec)
    {
        return std::nullopt;
    }

    NWindows::NCOM::CPropVariant prop;
    const HRESULT hr = archive->GetProperty(0, kpidSize, &prop);
    archive->Close();
    if (FAILED(hr) || prop.vt != VT_UI8)
    {
        return std::nullopt;
    }
    return static_cast<uint64_t>(prop.uhVal.QuadPart);
}

std::error_code SevenZipCodecImpl::Decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output)
{
    if (input.empty())
    {
        return make_error_code(errc::empty_payload);
    }

    std::error_code ec;
    CMyComPtr<IInArchive> archive = OpenArchive(input, ec);
    if (ec)
    {
        return ec;
    }

    std::vector<uint8_t> result;
    sevenzip::ExtractCallback* callbackImpl = new sevenzip::ExtractCallback(result);
    CMyComPtr<IArchiveExtractCallback> callback = callbackImpl;

    const UInt32 index = 0;
    const HRESULT hr = archive->Extract(&index, 1, 0, callback);
    archive->Close();
    if (FAILED(hr))
    {
        Log::Debug(L"Failed IInArchive::Extract [{}]", FormatError(HResultError(hr)));
        return make_error_code(errc::corrupt_payload);
    }
    if (callbackImpl->Failed())
    {
        return make_error_code(errc::corrupt_payload);
    }

    output = std::move(result);
    return {};
}

Codec& SevenZipCodec()
{
    static SevenZipCodecImpl codec;
    return codec;
}

}  // namespace rcedit
```

In `src/core/CMakeLists.txt`:
```cmake
if(RCEDIT_ENABLE_7Z)
    find_package(7zip CONFIG REQUIRED)
    target_sources(rcedit_core PRIVATE
        codec_7z.h
        codec_7z.cpp
        sevenzip/sevenzip.h
        sevenzip/in_mem_stream.h
        sevenzip/in_mem_stream.cpp
        sevenzip/out_mem_stream.h
        sevenzip/out_mem_stream.cpp
        sevenzip/update_callback.h
        sevenzip/update_callback.cpp
        sevenzip/extract_callback.h
        sevenzip/extract_callback.cpp
    )
    target_link_libraries(rcedit_core PRIVATE 7zip::7zip 7zip::extras)
endif()
```

- [ ] **Step 5: Build `no-zstd` and run tests**

```powershell
cmake --build --preset no-zstd-MinSizeRel
ctest --preset no-zstd-MinSizeRel
```
Expected: `codec` passes including the three `SevenZip*` cases.

Link troubleshooting, in order:
1. `rcedit_tests` links with default libraries, so it should link cleanly. If it reports `_com_issue_error` unresolved, the `<comutil.h>` include in `extras.h` needs `comsuppw.lib` (a static lib, no DLL): add it next to `oleaut32.lib` in `src/cli/CMakeLists.txt` and to `rcedit_tests` in `tests/CMakeLists.txt`.
2. If a 7-Zip method override does not match the interface signature, open `build/no-zstd/vcpkg_installed/x64-windows-static/include/7zip/CPP/7zip/Archive/IArchive.h` and copy the exact declaration; keep the `STDMETHOD` form.

The exe does not reference the codec yet (that comes with `ops` in Task 8), so `imports` still shows only kernel32 in this task.

- [ ] **Step 6: Commit**

```powershell
clang-format -i src/core/*.cpp src/core/*.h src/core/sevenzip/*.cpp src/core/sevenzip/*.h
git add -A
git -c user.name=fabienfl -c user.email=fabien.fl-orc@ssi.gouv.fr commit -m "Add 7z codec with in-memory streams"
```

---

