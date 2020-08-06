// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_T931_T931_POWER_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_T931_T931_POWER_H_

#include <stdint.h>

enum class T931PowerDomains : uint32_t {
  kArmCoreBig = 0,
  kArmCoreLittle = 1,
};

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_T931_T931_POWER_H_
