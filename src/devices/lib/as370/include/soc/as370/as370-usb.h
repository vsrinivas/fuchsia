// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_USB_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_USB_H_

#include <hwreg/bitfields.h>

namespace as370 {

constexpr uint32_t kUsb0Base = 0xf7ed0000;
constexpr uint32_t kUsb0Size = 0x8000;

constexpr uint32_t kUsbPhy0Base = 0xf7ed8000;
constexpr uint32_t kUsbPhy0Size = 0x100;

constexpr uint32_t kUsb0Irq = 19 + 32;

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

class USB_PHY_RB : public hwreg::RegisterBase<USB_PHY_RB, uint32_t> {
 public:
  DEF_BIT(0, clk_rdy);
  static auto Get() { return hwreg::RegisterAddr<USB_PHY_RB>(0x10); }
};

}  // namespace as370

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_USB_H_
