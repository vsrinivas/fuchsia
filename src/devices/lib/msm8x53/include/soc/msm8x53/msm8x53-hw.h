// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_MSM8X53_INCLUDE_SOC_MSM8X53_MSM8X53_HW_H_
#define SRC_DEVICES_LIB_MSM8X53_INCLUDE_SOC_MSM8X53_MSM8X53_HW_H_

namespace msm8x53 {

// IRQ Table.

constexpr uint32_t kIrqSdc1 = 155;

// kIrqCombined is called "summary" in the docs (as opposed to the 8 dedicted "direct" interrupts).
constexpr uint32_t kIrqCombined = 240;
constexpr uint32_t kIrqDirect7 = 241;
constexpr uint32_t kIrqDirect6 = 242;
constexpr uint32_t kIrqDirect5 = 243;
constexpr uint32_t kIrqDirect4 = 244;
constexpr uint32_t kIrqDirect3 = 245;
constexpr uint32_t kIrqDirect2 = 246;
constexpr uint32_t kIrqDirect1 = 247;
constexpr uint32_t kIrqDirect0 = 248;

}  // namespace msm8x53

#endif  // SRC_DEVICES_LIB_MSM8X53_INCLUDE_SOC_MSM8X53_MSM8X53_HW_H_
