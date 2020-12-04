// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_REGISTERS_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_REGISTERS_H_

namespace aml_registers {

enum RegisterId : uint32_t {
  REGISTER_USB_PHY_V2_RESET,
  REGISTER_NNA_RESET_LEVEL2,
  REGISTER_MALI_RESET,
#ifdef FACTORY_BUILD
  REGISTER_USB_PHY_FACTORY,
#endif  // FACTORY_BUILD

  REGISTER_ID_COUNT,
};

// REGISTER_USB_PHY_V2_RESET constants
constexpr uint32_t USB_RESET1_REGISTER_UNKNOWN_1_MASK = 0x4;
constexpr uint32_t USB_RESET1_REGISTER_UNKNOWN_2_MASK = 0x1'0000;
constexpr uint32_t USB_RESET1_LEVEL_MASK = 0x3'0000;

// REGISTER_NNA_RESET_LEVEL2 constants
constexpr uint32_t NNA_RESET2_LEVEL_MASK = 0x1000;

// REGISTER_MALI_RESET constants
constexpr uint32_t MALI_RESET0_MASK = 0x100000;
constexpr uint32_t MALI_RESET2_MASK = 0x4000;

}  // namespace aml_registers

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_REGISTERS_H_
