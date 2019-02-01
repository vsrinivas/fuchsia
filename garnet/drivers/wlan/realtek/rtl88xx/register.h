// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef GARNET_DRIVERS_WLAN_REALTEK_RTL88XX_REGISTER_H_
#define GARNET_DRIVERS_WLAN_REALTEK_RTL88XX_REGISTER_H_

#include <wlan/common/bitfield.h>

namespace wlan {
namespace rtl88xx {

// This is a template class that represents an I/O register for a Realtek rtl88xx chip, templated on
// the register with and mapping offset.
template <typename ValueTypeT, uint16_t Offset>
class Register : public common::AddressableBitField<uint16_t, ValueTypeT, Offset> {
   public:
    using ValueType = ValueTypeT;
    constexpr Register() = default;
};

}  // namespace rtl88xx
}  // namespace wlan

#endif  // GARNET_DRIVERS_WLAN_REALTEK_RTL88XX_REGISTER_H_
