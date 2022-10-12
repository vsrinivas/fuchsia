// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_REGISTERS_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_REGISTERS_H_

#include <stdint.h>

namespace as370 {

enum RegisterId : uint32_t {
  AS370_TOP_STICKY_RESETN,
  EMMC_RESET,
  REGISTER_ID_COUNT,
};

}  // namespace as370

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_REGISTERS_H_
