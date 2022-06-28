// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_POWER_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_POWER_H_

#include <vector>

constexpr uint32_t kAs370NumPowerDomains = 1;

enum As370PowerDomains {
  kBuckSoC, /* SY20212D/SY8824, SoC VDD*/
};

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_POWER_H_
