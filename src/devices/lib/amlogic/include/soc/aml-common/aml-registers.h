// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_REGISTERS_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_REGISTERS_H_

namespace registers {

enum RegisterId : uint64_t {
#ifdef FACTORY_BUILD
  REGISTER_USB_PHY_FACTORY,
#endif  // FACTORY_BUILD

  REGISTER_ID_COUNT,
};

}  // namespace registers

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_REGISTERS_H_
