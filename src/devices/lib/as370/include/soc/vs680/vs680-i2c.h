// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_I2C_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_I2C_H_

#include <stdint.h>

namespace vs680 {

constexpr uint32_t kI2c0Base = 0xf7e8'1800;
constexpr uint32_t kI2c1Base = 0xf7e8'2000;
constexpr uint32_t kI2cSize = 0x100;

constexpr uint32_t kI2c0Irq = 80 + 32;
constexpr uint32_t kI2c1Irq = 81 + 32;

constexpr uint32_t kI2c0Sda = 46;
constexpr uint32_t kI2c0Scl = 47;
constexpr uint32_t kI2c0AltFunction = 1;

constexpr uint32_t kI2c1Sda = 51;
constexpr uint32_t kI2c1Scl = 52;
constexpr uint32_t kI2c1AltFunction = 3;

}  // namespace vs680

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_I2C_H_
