// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_GPIO_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_GPIO_H_

#include <limits.h>

#include <fbl/algorithm.h>

namespace as370 {

constexpr uint32_t kGpio1Base = 0xf7e8'1800;
constexpr uint32_t kGpio2Base = 0xf7e8'2000;
constexpr uint32_t kGpioSize = fbl::round_up<uint32_t, uint32_t>(0x200, PAGE_SIZE);

constexpr uint32_t kPinmuxBase = 0xf7ea'0840;
constexpr uint32_t kPinmuxSize = fbl::round_up<uint32_t, uint32_t>(0x20, PAGE_SIZE);

constexpr uint32_t kGpio1Irq = 40 + 32;
constexpr uint32_t kGpio2Irq = 41 + 32;

}  // namespace as370

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_GPIO_H_
