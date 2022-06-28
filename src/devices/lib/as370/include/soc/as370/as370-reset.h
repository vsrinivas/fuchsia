// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_RESET_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_RESET_H_

#include <hwreg/bitfields.h>

namespace as370 {

constexpr uint32_t kResetBase = 0xf7ea0000;
constexpr uint32_t kResetSize = 0x10000;

class GblPerifStickyResetN : public hwreg::RegisterBase<GblPerifStickyResetN, uint32_t> {
 public:
  DEF_BIT(6, pcie1PhyRstn);
  DEF_BIT(5, pcie0PhyRstn);
  DEF_BIT(4, usbOtgPhyreset);
  DEF_BIT(3, usbOtgHresetn);
  DEF_BIT(2, usbOtgPrstn);
  DEF_BIT(1, pcie1Rstn);
  DEF_BIT(0, pcie0Rstn);
  static auto Get() { return hwreg::RegisterAddr<GblPerifStickyResetN>(0x508); }
};

}  // namespace as370

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_RESET_H_
