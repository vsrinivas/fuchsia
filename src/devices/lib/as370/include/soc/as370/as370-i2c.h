// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_I2C_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_I2C_H_

#include <limits.h>

#include <fbl/algorithm.h>

namespace as370 {

constexpr uint32_t kI2c0Base = 0xf7e8'2800;
constexpr uint32_t kI2c0Size = fbl::round_up<uint32_t, uint32_t>(0x100, PAGE_SIZE);
constexpr uint32_t kI2c1Base = 0xf7e8'0800;
constexpr uint32_t kI2c1Size = fbl::round_up<uint32_t, uint32_t>(0x100, PAGE_SIZE);

constexpr uint32_t kI2c0Irq = 74;
constexpr uint32_t kI2c1Irq = 75;

constexpr uint32_t kI2c0Sda = 36;
constexpr uint32_t kI2c0Scl = 35;
constexpr uint32_t kI2c1Sda = 34;
constexpr uint32_t kI2c1Scl = 33;

}  // namespace as370

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_I2C_H_
