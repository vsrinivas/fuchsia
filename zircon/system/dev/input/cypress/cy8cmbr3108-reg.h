// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_INPUT_CYPRESS_CY8CMBR3108_REG_H_
#define ZIRCON_SYSTEM_DEV_INPUT_CYPRESS_CY8CMBR3108_REG_H_

#include <hwreg/i2c.h>

class SENSOR_EN
    : public hwreg::I2cRegisterBase<SENSOR_EN, uint16_t, sizeof(uint8_t), hwreg::LittleEndian> {
 public:
  static auto Get() { return hwreg::I2cRegisterAddr<SENSOR_EN>(0x00); }
};

class BUTTON_STAT
    : public hwreg::I2cRegisterBase<BUTTON_STAT, uint16_t, sizeof(uint8_t), hwreg::LittleEndian> {
 public:
  static auto Get() { return hwreg::I2cRegisterAddr<BUTTON_STAT>(0xAA); }
};

#endif  // ZIRCON_SYSTEM_DEV_INPUT_CYPRESS_CY8CMBR3108_REG_H_
