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
    Z7_COM_UNKNOWN_IMP_1( IArchiveUpdateCallback )
    Z7_IFACE_COM7_IMP( IProgress )
    Z7_IFACE_COM7_IMP( IArchiveUpdateCallback )

    UpdateCallback( std::span< const uint8_t > content, std::wstring name );

    [[nodiscard]] bool Failed() const noexcept { return m_failed; }

private:
    std::span< const uint8_t > m_content;
    std::wstring m_name;
    FILETIME m_time{};
    bool m_failed = false;
};

}  // namespace rcedit::sevenzip
