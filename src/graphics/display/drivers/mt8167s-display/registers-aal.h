// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_REGISTERS_AAL_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_REGISTERS_AAL_H_

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

//////////////////////////////////////////////////
// AAL Registers
//////////////////////////////////////////////////
#define AAL_EN (0x0000)
#define AAL_CFG (0x0020)
#define AAL_SIZE (0x0030)

namespace mt8167s_display {

class AalEnReg : public hwreg::RegisterBase<AalEnReg, uint32_t> {
 public:
  DEF_BIT(0, enable);
  static auto Get() { return hwreg::RegisterAddr<AalEnReg>(AAL_EN); }
};

class AalCfgReg : public hwreg::RegisterBase<AalCfgReg, uint32_t> {
 public:
  DEF_BIT(0, relay);
  static auto Get() { return hwreg::RegisterAddr<AalCfgReg>(AAL_CFG); }
};

class AalSizeReg : public hwreg::RegisterBase<AalSizeReg, uint32_t> {
 public:
  DEF_FIELD(28, 16, hsize);
  DEF_FIELD(12, 0, vsize);
  static auto Get() { return hwreg::RegisterAddr<AalSizeReg>(AAL_SIZE); }
};

}  // namespace mt8167s_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_REGISTERS_AAL_H_
