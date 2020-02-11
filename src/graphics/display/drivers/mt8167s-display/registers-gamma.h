// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_GAMMA_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_GAMMA_H_

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

//////////////////////////////////////////////////
// GAMMA Registers
//////////////////////////////////////////////////
#define GAMMA_EN (0x0000)
#define GAMMA_CFG (0x0020)
#define GAMMA_SIZE (0x0030)

namespace mt8167s_display {

class GammaEnReg : public hwreg::RegisterBase<GammaEnReg, uint32_t> {
 public:
  DEF_BIT(0, enable);
  static auto Get() { return hwreg::RegisterAddr<GammaEnReg>(GAMMA_EN); }
};

class GammaCfgReg : public hwreg::RegisterBase<GammaCfgReg, uint32_t> {
 public:
  DEF_BIT(0, relay);
  static auto Get() { return hwreg::RegisterAddr<GammaCfgReg>(GAMMA_CFG); }
};

class GammaSizeReg : public hwreg::RegisterBase<GammaSizeReg, uint32_t> {
 public:
  DEF_FIELD(28, 16, hsize);
  DEF_FIELD(12, 0, vsize);
  static auto Get() { return hwreg::RegisterAddr<GammaSizeReg>(GAMMA_SIZE); }
};

}  // namespace mt8167s_display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_GAMMA_H_
