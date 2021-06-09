// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A311D_A311D_POWER_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A311D_A311D_POWER_H_

#include <stdint.h>

enum class A311dPowerDomains : uint32_t {
  kArmCoreBig = 0,
  kArmCoreLittle = 1,
};

static constexpr uint32_t kMinVoltageUv = 680'000;
static constexpr uint32_t kMaxVoltageUv = 1'040'000;
static_assert(kMaxVoltageUv >= kMinVoltageUv,
              "kMaxVoltageUv must be greater than or equal to kMinVoltageUv\n");

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A311D_A311D_POWER_H_
