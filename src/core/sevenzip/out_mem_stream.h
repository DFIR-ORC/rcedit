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
    Z7_COM_UNKNOWN_IMP_2( ISequentialOutStream, IOutStream )
    Z7_IFACE_COM7_IMP( ISequentialOutStream )
    Z7_IFACE_COM7_IMP( IOutStream )

    explicit OutMemStream( std::vector< uint8_t >& buffer ) noexcept;

private:
    std::vector< uint8_t >& m_buffer;
    size_t m_pos = 0;
};

}  // namespace rcedit::sevenzip
