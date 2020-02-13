// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_REGISTERS_DITHER_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_REGISTERS_DITHER_H_

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

//////////////////////////////////////////////////
// DITHER Registers
//////////////////////////////////////////////////
#define DITHER_EN (0x0000)
#define DITHER_CFG (0x0020)
#define DITHER_SIZE (0x0030)
#define DISP_REG_DITHER_0 (0x0100)
#define DISP_REG_DITHER(x) (0x0100 + (4 * x))

namespace mt8167s_display {

class DitherEnReg : public hwreg::RegisterBase<DitherEnReg, uint32_t> {
 public:
  DEF_BIT(0, enable);
  static auto Get() { return hwreg::RegisterAddr<DitherEnReg>(DITHER_EN); }
};

class DitherCfgReg : public hwreg::RegisterBase<DitherCfgReg, uint32_t> {
 public:
  DEF_BIT(1, dither_engine_en);
  DEF_BIT(0, relay);
  static auto Get() { return hwreg::RegisterAddr<DitherCfgReg>(DITHER_CFG); }
};

class DitherSizeReg : public hwreg::RegisterBase<DitherSizeReg, uint32_t> {
 public:
  DEF_FIELD(28, 16, hsize);
  DEF_FIELD(12, 0, vsize);
  static auto Get() { return hwreg::RegisterAddr<DitherSizeReg>(DITHER_SIZE); }
};

class DitherDReg : public hwreg::RegisterBase<DitherDReg, uint32_t> {
 public:
  static auto Get(uint32_t off) {
    return hwreg::RegisterAddr<DitherDReg>(DISP_REG_DITHER_0 + (off * 4));
  }
};

}  // namespace mt8167s_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_REGISTERS_DITHER_H_
