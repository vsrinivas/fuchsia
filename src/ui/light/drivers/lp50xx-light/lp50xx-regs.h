// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIGHT_DRIVERS_LP50XX_LIGHT_LP50XX_REGS_H_
#define SRC_UI_LIGHT_DRIVERS_LP50XX_LIGHT_LP50XX_REGS_H_
#include <hwreg/i2c.h>

namespace lp50xx_light {

class DeviceConfig0Reg : public hwreg::I2cRegisterBase<DeviceConfig0Reg, uint8_t, sizeof(uint8_t)> {
 public:
  DEF_BIT(6, chip_enable);
  static auto Get() { return hwreg::I2cRegisterAddr<DeviceConfig0Reg>(0x00); }
};

class DeviceConfig1Reg : public hwreg::I2cRegisterBase<DeviceConfig1Reg, uint8_t, sizeof(uint8_t)> {
 public:
  DEF_BIT(5, log_scale_enable);
  DEF_BIT(4, power_save_enable);
  DEF_BIT(3, auto_incr_enable);
  DEF_BIT(2, pwm_dithering_enable);
  DEF_BIT(1, max_current_option);
  DEF_BIT(0, led_gLobal_off);
  static auto Get() { return hwreg::I2cRegisterAddr<DeviceConfig1Reg>(0x01); }
};

class BrightnessReg : public hwreg::I2cRegisterBase<BrightnessReg, uint8_t, sizeof(uint8_t)> {
 public:
  DEF_FIELD(7, 0, brightness);

  static auto Get(uint32_t brightness_addr, uint32_t index) {
    return hwreg::I2cRegisterAddr<BrightnessReg>(brightness_addr + index);
  }
};

class BlueColorReg : public hwreg::I2cRegisterBase<BlueColorReg, uint8_t, sizeof(uint8_t)> {
 public:
  static auto Get(uint32_t blue_addr, uint32_t index) {
    return hwreg::I2cRegisterAddr<BlueColorReg>(blue_addr + (index * 3));
  }
};

class RedColorReg : public hwreg::I2cRegisterBase<RedColorReg, uint8_t, sizeof(uint8_t)> {
 public:
  static auto Get(uint32_t red_addr, uint32_t index) {
    return hwreg::I2cRegisterAddr<RedColorReg>(red_addr + (index * 3));
  }
};

class GreenColorReg : public hwreg::I2cRegisterBase<GreenColorReg, uint8_t, sizeof(uint8_t)> {
 public:
  static auto Get(uint32_t green_addr, uint32_t index) {
    return hwreg::I2cRegisterAddr<GreenColorReg>(green_addr + (index * 3));
  }
};

class ResetReg : public hwreg::I2cRegisterBase<ResetReg, uint8_t, sizeof(uint8_t)> {
 public:
  static auto Get(uint32_t reset_addr) { return hwreg::I2cRegisterAddr<ResetReg>(reset_addr); }
};

}  // namespace lp50xx_light

#endif  // SRC_UI_LIGHT_DRIVERS_LP50XX_LIGHT_LP50XX_REGS_H_
