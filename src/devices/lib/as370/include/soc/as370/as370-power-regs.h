// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_POWER_REGS_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_POWER_REGS_H_

#include <zircon/types.h>

#include <hwreg/i2c.h>

// SY20212D Silergy synchronous step down regulator
class BuckRegulatorRegister
    : public hwreg::I2cRegisterBase<BuckRegulatorRegister, uint8_t, sizeof(uint8_t)> {
 public:
  static constexpr uint32_t kMinVoltage = 762'500;      // in uV
  static constexpr uint32_t kMaxVoltage = 1'550'000;    // in uV
  static constexpr uint32_t kDefaultVoltage = 900'000;  // in uV
  static constexpr uint32_t kStepSize = 12'500;         // in uV

  // Software buck enable, 1 - ON(default)
  DEF_BIT(7, buck_enable);
  // 0 - auto PFM(default), 1 - Forced PWN
  DEF_BIT(6, mode);
  // Voltage Select - 111111b = 1.55V, 001011b = 0.9V(default), 000000b = 0.7625V
  DEF_FIELD(5, 0, voltage);
  static auto Get() { return hwreg::I2cRegisterAddr<BuckRegulatorRegister>(0x00); }
};

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_POWER_REGS_H_
