// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_COLOR_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_COLOR_H_

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

//////////////////////////////////////////////////
// GAMMA Registers
//////////////////////////////////////////////////
#define COLOR_MAIN (0x0400)
#define COLOR_START (0x0C00)
#define COLOR_WIDTH (0x0C50)
#define COLOR_HEIGHT (0x0C54)
#define COLOR_CM1_EN (0x0C60)
#define COLOR_CM2_EN (0x0CA0)

namespace mt8167s_display {

class ColorMainReg : public hwreg::RegisterBase<ColorMainReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<ColorMainReg>(COLOR_MAIN); }
};
class ColorStartReg : public hwreg::RegisterBase<ColorStartReg, uint32_t> {
 public:
  DEF_BIT(1, out_sel);
  DEF_BIT(0, start);
  static auto Get() { return hwreg::RegisterAddr<ColorStartReg>(COLOR_START); }
};
class ColorWidthReg : public hwreg::RegisterBase<ColorWidthReg, uint32_t> {
 public:
  DEF_FIELD(13, 0, width);
  static auto Get() { return hwreg::RegisterAddr<ColorWidthReg>(COLOR_WIDTH); }
};
class ColorHeightReg : public hwreg::RegisterBase<ColorHeightReg, uint32_t> {
 public:
  DEF_FIELD(13, 0, height);
  static auto Get() { return hwreg::RegisterAddr<ColorHeightReg>(COLOR_HEIGHT); }
};
class ColorCm1EnReg : public hwreg::RegisterBase<ColorCm1EnReg, uint32_t> {
 public:
  DEF_BIT(0, front_en);
  static auto Get() { return hwreg::RegisterAddr<ColorCm1EnReg>(COLOR_CM1_EN); }
};
class ColorCm2EnReg : public hwreg::RegisterBase<ColorCm2EnReg, uint32_t> {
 public:
  DEF_BIT(0, back_en);
  static auto Get() { return hwreg::RegisterAddr<ColorCm2EnReg>(COLOR_CM2_EN); }
};

}  // namespace mt8167s_display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_COLOR_H_
