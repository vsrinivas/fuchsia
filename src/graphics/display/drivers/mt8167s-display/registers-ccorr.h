// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_CCORR_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_CCORR_H_

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

//////////////////////////////////////////////////
// CCORR Registers
//////////////////////////////////////////////////
#define CCORR_EN (0x0000)
#define CCORR_CFG (0x0020)
#define CCORR_SIZE (0x0030)

namespace mt8167s_display {

class CcorrEnReg : public hwreg::RegisterBase<CcorrEnReg, uint32_t> {
 public:
  DEF_BIT(0, enable);
  static auto Get() { return hwreg::RegisterAddr<CcorrEnReg>(CCORR_EN); }
};

class CcorrCfgReg : public hwreg::RegisterBase<CcorrCfgReg, uint32_t> {
 public:
  DEF_BIT(0, relay);
  static auto Get() { return hwreg::RegisterAddr<CcorrCfgReg>(CCORR_CFG); }
};

class CcorrSizeReg : public hwreg::RegisterBase<CcorrSizeReg, uint32_t> {
 public:
  DEF_FIELD(28, 16, hsize);
  DEF_FIELD(12, 0, vsize);
  static auto Get() { return hwreg::RegisterAddr<CcorrSizeReg>(CCORR_SIZE); }
};

}  // namespace mt8167s_display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_CCORR_H_
