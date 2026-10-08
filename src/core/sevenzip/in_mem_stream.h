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
    Z7_COM_UNKNOWN_IMP_2( ISequentialInStream, IInStream )
    Z7_IFACE_COM7_IMP( ISequentialInStream )
    Z7_IFACE_COM7_IMP( IInStream )

    explicit InMemStream( std::span< const uint8_t > buffer ) noexcept;

private:
    std::span< const uint8_t > m_buffer;
    size_t m_pos = 0;
};

}  // namespace rcedit::sevenzip
