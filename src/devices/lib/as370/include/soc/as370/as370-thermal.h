// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_THERMAL_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_THERMAL_H_

#include <limits.h>

#include <fbl/algorithm.h>

namespace as370 {

constexpr uint32_t kThermalBase = 0xf7ea'0800;
constexpr uint32_t kThermalSize = fbl::round_up<uint32_t, uint32_t>(0x1c, PAGE_SIZE);

}  // namespace as370

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_THERMAL_H_
