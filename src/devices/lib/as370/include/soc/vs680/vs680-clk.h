// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_CLK_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_CLK_H_

#include <stdint.h>

namespace vs680 {

constexpr uint32_t kCpuPllBase = 0xf792'2000;
constexpr uint32_t kCpuPllSize = 0x20;

constexpr uint32_t kAvioBase = 0xf74d'4000;
constexpr uint32_t kAvioSize = 0x200;

enum ClockMmio : uint8_t {
  kChipCtrlMmio = 0,  // SYSPLLs
  kCpuPllMmio,
  kAvioMmio,  // APLLs and VPLLs
  kMmioCount,
};

enum Clock : uint32_t {
  kRefClock = 0,
  kSysPll0,
  kSysPll1,
  kSysPll2,
  kCpuPll,
  kVPll0,
  kVPll1,
  kAPll0,
  kAPll1,
  kSd0Clock,
  kClockCount,
};

enum ClockInput : uint32_t {
  kClockInputSysPll2 = 0,
  kClockInputSysPll0F = 1,
  kClockInputSysPll1F = 2,
  kClockInputSysPll2F = 3,
  kClockInputSysPll1 = 4,
  kClockInputSysPll0,
  kClockInputCount,
};

}  // namespace vs680

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_CLK_H_
