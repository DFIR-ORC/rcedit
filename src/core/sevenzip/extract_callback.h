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
    Z7_COM_UNKNOWN_IMP_1( IArchiveExtractCallback )
    Z7_IFACE_COM7_IMP( IProgress )
    Z7_IFACE_COM7_IMP( IArchiveExtractCallback )

    explicit ExtractCallback( std::vector< uint8_t >& output ) noexcept;

    [[nodiscard]] bool Failed() const noexcept { return m_failed; }

private:
    std::vector< uint8_t >& m_output;
    bool m_failed = false;
};

}  // namespace rcedit::sevenzip
