// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <type_traits>

#include "garnet/drivers/wlan/common/bitfield.h"

namespace ralink {

using wlan::common::AddressableBitField;
using wlan::common::BitField;

template <uint16_t A> class Register : public AddressableBitField<uint16_t, uint32_t, A> {
   public:
    constexpr Register() = default;
};

template <uint16_t A> class EepromField : public AddressableBitField<uint16_t, uint16_t, A> {
   public:
    constexpr explicit EepromField(uint16_t val)
        : AddressableBitField<uint16_t, uint16_t, A>(val) {}
    constexpr EepromField() = default;
};

template <uint8_t A> class BbpRegister : public AddressableBitField<uint8_t, uint8_t, A> {
   public:
    constexpr explicit BbpRegister(uint8_t val) : AddressableBitField<uint8_t, uint8_t, A>(val) {}
    constexpr BbpRegister() = default;
};

template <uint8_t A> class RfcsrRegister : public AddressableBitField<uint8_t, uint8_t, A> {
   public:
    constexpr explicit RfcsrRegister(uint8_t val) : AddressableBitField<uint8_t, uint8_t, A>(val) {}
    constexpr RfcsrRegister() = default;
};

}  // namespace ralink
