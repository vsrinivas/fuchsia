// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_NAND_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_NAND_H_

#include <limits.h>

#include <fbl/algorithm.h>

namespace as370 {

constexpr uint32_t kNandBase = 0xf7f1'0000;
constexpr uint32_t kNandSize = fbl::round_up<uint32_t, uint32_t>(0x2084, PAGE_SIZE);

constexpr uint32_t kNandFifoBase = 0xf7f0'0000;
constexpr uint32_t kNandFifoSize = PAGE_SIZE;

constexpr uint32_t kNandIrq = 20 + 32;

}  // namespace as370

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_NAND_H_
