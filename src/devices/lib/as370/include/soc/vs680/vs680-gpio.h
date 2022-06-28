// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_GPIO_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_GPIO_H_

#include <limits.h>

#include <fbl/algorithm.h>

namespace vs680 {

constexpr uint32_t kGpio1Base = 0xf7e8'2400;
constexpr uint32_t kGpio2Base = 0xf7e8'0800;
constexpr uint32_t kSmGpioBase = 0xf7fc'8000;
constexpr uint32_t kGpioSize = fbl::round_up<uint32_t, uint32_t>(0x400, PAGE_SIZE);

constexpr uint32_t kSocPinmuxBase = 0xf7ea'8000;
constexpr uint32_t kAvioPinmuxBase = 0xf7ea'8400;
constexpr uint32_t kSmPinmuxBase = 0xf7fe'2c10;
constexpr uint32_t kPinmuxSize = fbl::round_up<uint32_t, uint32_t>(0x10, PAGE_SIZE);

constexpr uint32_t kGpio1Irq = 77 + 32;
constexpr uint32_t kGpio2Irq = 78 + 32;

}  // namespace vs680

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_GPIO_H_
