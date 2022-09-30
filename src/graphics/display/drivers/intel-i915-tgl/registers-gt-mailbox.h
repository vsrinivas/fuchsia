// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_GT_MAILBOX_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_GT_MAILBOX_H_

#include <cstdint>

#include <hwreg/bitfields.h>

namespace tgl_registers {

// GTDRIVER_MAILBOX_INTERFACE (GT Driver Mailbox Interface).
//
// Used for communication between the graphics driver and the PCODE (power
// controller firmware code) running on the PCU (power controller).
//
// This register is documented in IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 1090
// and IHD-OS-DG1-Vol 2c-2.21 Part 1 page 1049, but the MMIO address is
// incorrect.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 1089
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 1 page 1049
// Kaby Lake: IHD-OS-KBL-Vol 12-1.17 "Sequences for Changing CD Clock Frequency,
//            pages 138-139
// Skylake: IHD-OS-SKL-Vol 12-05.16 "Skylake Sequences for Changing CD Clock
//          Frequency", pages 135-136
class PowerMailboxInterface : public hwreg::RegisterBase<PowerMailboxInterface, uint32_t> {
 public:
  // True if the PCU is currently executing a command from the graphics driver.
  //
  // The driver sets this field to true to ask the PCU (power control unit)
  // firmware to execute a command. The data registers must be set to correct
  // values before setting this to true.
  //
  // The PCU firmware sets this field to false when it completes the command.
  DEF_BIT(31, has_active_transaction);

  // The command to be executed by the PCU.
  //
  // Valid commands are documented throughout the reference manuals.
  DEF_FIELD(30, 0, command_code);

  static auto Get() { return hwreg::RegisterAddr<PowerMailboxInterface>(0x138124); }
};

// GTDRIVER_MAILBOX_DATA0 (GT Driver Mailbox Data0 / Data Low)
//
// Used for communication between the graphics driver and the PCODE (power
// controller firmware code) running on the PCU (power controller).
//
// This register must not be modified while the PCU is executing a driver
// command, as indicated in the PowerMailboxInterface register.
//
// Kaby Lake: IHD-OS-KBL-Vol 12-1.17 "Sequences for Changing CD Clock Frequency,
//            pages 138-139
// Skylake: IHD-OS-SKL-Vol 12-05.16 "Skylake Sequences for Changing CD Clock
//          Frequency", pages 135-136
class PowerMailboxData0 : public hwreg::RegisterBase<PowerMailboxData0, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PowerMailboxData0>(0x138128); }
};

// GTDRIVER_MAILBOX_DATA1 (GT Driver Mailbox Data1 / Data High)
//
// Used for communication between the graphics driver and the PCODE (power
// controller firmware code) running on the PCU (power controller).
//
// This register must not be modified while the PCU is executing a driver
// command, as indicated in the PowerMailboxInterface register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 1090
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 1 page 1048
// Kaby Lake: IHD-OS-KBL-Vol 12-1.17 "Sequences for Changing CD Clock Frequency,
//            pages 138-139
// Skylake: IHD-OS-SKL-Vol 12-05.16 "Skylake Sequences for Changing CD Clock
//          Frequency", pages 135-136
class PowerMailboxData1 : public hwreg::RegisterBase<PowerMailboxData1, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PowerMailboxData0>(0x13812c); }
};

}  // namespace tgl_registers

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_GT_MAILBOX_H_
