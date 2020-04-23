// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_USB_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_USB_H_

#include <hwreg/bitfields.h>

namespace vs680 {

constexpr uint32_t kUsb0Base = 0xf7c00000;
constexpr uint32_t kUsb0Size = 0x40000;

constexpr uint32_t kUsbPhy0Base = 0xf7c40000;
constexpr uint32_t kUsbPhy0Size = 0x100;

constexpr uint32_t kUsb0Irq = 11 + 32;

class USB_PHY_CTRL0 : public hwreg::RegisterBase<USB_PHY_CTRL0, uint32_t> {
 public:
  DEF_FIELD(31, 0, value);
  static auto Get() { return hwreg::RegisterAddr<USB_PHY_CTRL0>(0x0); }
};

class USB_PHY_CTRL1 : public hwreg::RegisterBase<USB_PHY_CTRL1, uint32_t> {
 public:
  DEF_FIELD(31, 0, value);
  static auto Get() { return hwreg::RegisterAddr<USB_PHY_CTRL1>(0x4); }
};

}  // namespace vs680

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_USB_H_
