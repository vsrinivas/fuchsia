// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_RAM_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_RAM_H_

#include <stdint.h>

namespace aml_ram {

// TODO(cpu): Understand why we use these two numbers.
constexpr uint64_t kMemCycleCount = 1024 * 1024 * 57u;
constexpr double kMemCyclePerSecond = (912.0 / 2.0);

// Astro and Sherlock ports.
constexpr uint64_t kPortIdArmAe = 0x01u << 0;
constexpr uint64_t kPortIdMali = 0x01u << 1;
constexpr uint64_t kPortIdPcie = 0x01u << 2;
constexpr uint64_t kPortIdHdcp = 0x01u << 3;
constexpr uint64_t kPortIdHevcFront = 0x01u << 4;
constexpr uint64_t kPortIdTest = 0x01u << 5;
constexpr uint64_t kPortIdUsb30 = 0x01u << 6;
constexpr uint64_t kPortIdHevcBack = 0x01u << 8;
constexpr uint64_t kPortIdH265Enc = 0x01u << 9;
constexpr uint64_t kPortIdVpuR1 = 0x01u << 16;
constexpr uint64_t kPortIdVpuR2 = 0x01u << 17;
constexpr uint64_t kPortIdVpuR3 = 0x01u << 18;
constexpr uint64_t kPortIdVpuW1 = 0x01u << 19;
constexpr uint64_t kPortIdVpuW2 = 0x01u << 20;
constexpr uint64_t kPortIdVDec = 0x01u << 21;
constexpr uint64_t kPortIdHCodec = 0x01u << 22;
constexpr uint64_t kPortIdGe2D = 0x01u << 23;
// Sherlock-only ports.
constexpr uint64_t kPortIdNNA = 0x01u << 10;
constexpr uint64_t kPortIdGDC = 0x01u << 11;
constexpr uint64_t kPortIdMipiIsp = 0x01u << 12;
constexpr uint64_t kPortIdArmAf = 0x01u << 13;

constexpr uint64_t kDefaultChannelCpu = kPortIdArmAe;
constexpr uint64_t kDefaultChannelGpu = kPortIdMali;
constexpr uint64_t kDefaultChannelVDec =
    kPortIdHevcFront | kPortIdHevcBack | kPortIdVDec | kPortIdHCodec;
constexpr uint64_t kDefaultChannelVpu =
    kPortIdVpuR1 | kPortIdVpuR2 | kPortIdVpuR3 | kPortIdVpuW1 | kPortIdVpuW2;

inline double CounterToBandwidth(uint64_t counter) {
  return (static_cast<double>(counter) * kMemCyclePerSecond) / kMemCycleCount;
}

}  // namespace aml_ram

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_RAM_H_
