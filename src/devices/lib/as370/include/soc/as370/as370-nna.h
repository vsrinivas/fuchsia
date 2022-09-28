// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_NNA_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_NNA_H_

#include <limits.h>

#include <fbl/algorithm.h>

namespace as370 {
// Gbl_topStickyResetN
constexpr uint32_t kNnaResetOffset = 0x0504;
constexpr uint32_t kNnaResetMask = 0b0000'0001;

// Gbl_SRAM_PWR_CTRL_NNA
constexpr uint32_t kNnaPowerOffset = 0x006C;
constexpr uint32_t kNnaPowerMask = 0b0000'0011;

// Gbl_nnaSysClk_ctrl
constexpr uint32_t kNnaClockSysOffset = 0x0538;
constexpr uint32_t kNnaClockSysMask = 0b0011'1111'1111;

// Gbl_nnaCoreClk_ctrl
constexpr uint32_t kNnaClockCoreOffset = 0x053C;
constexpr uint32_t kNnaClockCoreMask = 0b0011'1111'1111;

// NNA Sub-System Registers.
constexpr uint32_t kNnaBase = 0xf7f3'0000;
constexpr uint32_t kNnaSize = fbl::round_up<uint32_t, uint32_t>(0x1'0000, PAGE_SIZE);
constexpr uint32_t kNnaIrq = (32 + 66);  // offset + IRQ 66 from dtsi

}  // namespace as370

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_NNA_H_
