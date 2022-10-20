// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_REGISTERS_GT_MAILBOX_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_REGISTERS_GT_MAILBOX_H_

#include <cstdint>

#include <hwreg/bitfields.h>

namespace registers {

// GTDRIVER_MAILBOX_INTERFACE (GT Driver Mailbox Interface).
//
// Used for communication between the graphics driver and the PCODE (power
// controller firmware code) running on the PCU (power controller).
//
// This register's field breakdown was last documented in the Broadwell
// documentation (IHD-OS-BDW-Vol 12-11.15 pages 31-32).
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 1090
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 1 page 1049
// Kaby Lake: IHD-OS-KBL-Vol 12-1.17 "Sequences for Changing CD Clock
//            Frequency", pages 138-139
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
  //
  // This field is also called RUN_BUSY (Run/Busy) in Intel's documentation.
  DEF_BIT(31, has_active_transaction);

  // The Broadwell PCU firmware had bits 28:8 assigned to an Address Control
  // field (documented in IHD-OS-BDW-Vol 12-11.15 pages 31-32). The Address
  // Control field appears to still be in use on Kaby Lake and Skylake, because
  // it's mentioned in section "System Agent Geyserville (SAGV)" > "Memory
  // Values" > "Retrieve Memory Latency Data" in the display engine PRM.
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 page 208
  // Skylake: IHD-OS-SKL-Vol 12-05.16 page 199
  //
  // We can get away with ignoring the Address Control field because the only
  // command description that references it sets all its bits to zero.
  DEF_RSVDZ_FIELD(30, 24);

  // The PARAM2 field in Intel's documentation.
  //
  // This field is mentioned in the display engine PRMs, but its underlying bits
  // are not documented. We deduced its placement by comparing the
  // icl_pcode_read_qgv_point_info() function in the i915 OpenBSD driver with
  // against the MAILBOX_GTRDIVER_CMD_MEM_SS_INFO_SUBCOMMAND_READ_QGV_POINT_INFO
  // description in Tiger Lake documentation.
  DEF_FIELD(23, 16, param2);

  // The PARAM1 field in Intel documentation.
  //
  // This field is documented implicitly by a mention of "PARAM1[15:8]" in the
  // MAILBOX_GTRDIVER_CMD_MEM_SS_INFO command description under the "Mailbox
  // Commands" section of the display engine PRM.
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 page 212
  // DG1: IHD-OS-DG1-Vol 12-2.21 page 169
  // Lakefield: IHD-OS-LKF-Vol 12-4.21 page 166
  DEF_FIELD(15, 8, param1);

  // The command to be executed by the PCU.
  //
  // Valid commands are documented throughout the reference manuals.
  //
  // This field is also called COMMAND in Intel's documentation.
  DEF_FIELD(7, 0, command_code);

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
// This register was last documented formally in the Broadwell documentation
// (IHD-OS-BDW-Vol 12-11.15 page 33). Later PRMs document it indirectly, by
// providing its MMIO address in programming sequences.
//
// Kaby Lake: IHD-OS-KBL-Vol 12-1.17 "Sequences for Changing CD Clock
//            Frequency", pages 138-139
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
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 page 1089
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 1 page 1048
// Kaby Lake: IHD-OS-KBL-Vol 12-1.17 "Sequences for Changing CD Clock
//            Frequency", pages 138-139
// Skylake: IHD-OS-SKL-Vol 12-05.16 "Skylake Sequences for Changing CD Clock
//          Frequency", pages 135-136
class PowerMailboxData1 : public hwreg::RegisterBase<PowerMailboxData1, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PowerMailboxData0>(0x13812c); }
};

}  // namespace registers

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_REGISTERS_GT_MAILBOX_H_
