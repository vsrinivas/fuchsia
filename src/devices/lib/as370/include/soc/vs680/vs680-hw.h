// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_HW_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_HW_H_

#include <limits.h>

#include <fbl/algorithm.h>

namespace vs680 {

constexpr uint32_t kChipCtrlBase = 0xf7ea'0000;
constexpr uint32_t kChipCtrlSize = 0x800;

// SDIO Registers
constexpr uint32_t kEmmc0Base = 0xf7aa'0000;
constexpr uint32_t kEmmc0Size = fbl::round_up<uint32_t, uint32_t>(0x1000, PAGE_SIZE);
constexpr uint32_t kEmmc0Irq = (13 + 32);

constexpr uint32_t kSdioBase = 0xf7ab'0000;
constexpr uint32_t kSdioSize = 0x1000;
constexpr uint32_t kSdioIrq = 15 + 32;

constexpr uint32_t kCpuWrpBase = 0xf792'0000;
constexpr uint32_t kCpuWrpSize = 0x1000;
constexpr uint32_t kTempSensorIrq = 72 + 32;

}  // namespace vs680

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_HW_H_
