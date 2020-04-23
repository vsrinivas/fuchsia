// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_RESET_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_RESET_H_

#include <hwreg/bitfields.h>

namespace vs680 {

constexpr uint32_t kResetBase = 0xf7ea0000;
constexpr uint32_t kResetSize = 0x10000;

class Gbl_perifReset : public hwreg::RegisterBase<Gbl_perifReset, uint32_t> {
 public:
  DEF_BIT(9, usb0SyncReset);
  static auto Get() { return hwreg::RegisterAddr<Gbl_perifReset>(0x680); }
};

class Gbl_perifStickyResetN : public hwreg::RegisterBase<Gbl_perifStickyResetN, uint32_t> {
 public:
  DEF_BIT(4, usb0MahbRstn);
  DEF_BIT(3, usb0CoreRstn);
  DEF_BIT(2, usb0PhyRstn);
  static auto Get() { return hwreg::RegisterAddr<Gbl_perifStickyResetN>(0x688); }
};

class ClockReg700 : public hwreg::RegisterBase<ClockReg700, uint32_t> {
 public:
  DEF_BIT(0, usb0coreclkEn);
  static auto Get() { return hwreg::RegisterAddr<ClockReg700>(0x700); }
};

}  // namespace vs680

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_RESET_H_
