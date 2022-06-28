// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_POWER_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_POWER_H_

#include <stdint.h>

namespace vs680 {

enum PowerDomain : uint32_t {
  kPowerDomainVCpu = 0,
  kPowerDomainCount,
};

}  // namespace vs680

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_VS680_VS680_POWER_H_
