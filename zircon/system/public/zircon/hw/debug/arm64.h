// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_HW_DEBUG_ARM64_H_
#define SYSROOT_ZIRCON_HW_DEBUG_ARM64_H_

#include <stdint.h>

// ARM64 Hardware Debug Resources
// =================================================================================================

// Hardware Breakpoints ----------------------------------------------------------------------------
//
// Hardware breakpoints permits to stop a thread when it executes an address setup in one of the
// hw breakpoints registers. They will work independent whether the address in question is
// read-only or not.

// ARMv8 assures at least 2 hardware breakpoints.
#define ARM64_MIN_HW_BREAKPOINTS 2
#define ARM64_MAX_HW_BREAKPOINTS 16

// Access macros:
// All the relevant register fields are exposed through macros.
// For convenience of use, use the get/set macros:
//
// uint64_t ARM64_<REG>_<FIELD>_GET(uint64_t reg)
// void ARM64_<REG>_<FIELD>_SET(uint64_t* reg, uint64_t value)
//
// Examples:
// uint64_t bas = ARM64_DBGBCR_BAS_GET(dbgbcr);
// ARM64_DBGBCR_BAS_SET(&dbgbcr, modified_bas);

// DBGBCR<n>: Debug Control Register for HW Breakpoint #n.
//
// Control register for HW breakpoints. There is one foreach HW breakpoint present within the
// system. They go numbering by DBGBCR0, DBGBCR1, ... until the value defined in ID_AADFR0_EL1.
//
// For each control register, there is an equivalent DBGBVR<n> that holds the address the thread
// will compare against.

// The following fields are accepted by the user. All other fields are ignored (masked).
//
// - E

// This mask is applied when to DBGBCR. Any set values on those fields will be overwritten.
//
// - PMC = 0b10
// - BAS = 0b1111
// - HMC = 0
// - SSC = 0
// - LBN = 0
// - BT  = 0

// Enable/disable the breakpoint.
#define ARM64_DBGBCR_E 1lu  // Bit 0
#define ARM64_DBGBCR_E_SHIFT 0u
#define ARM64_DBGBCR_E_MASK (ARM64_DBGBCR_E << ARM64_DBGBCR_E_SHIFT)
#define ARM64_DBGBCR_E_GET(dbgbcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgbcr), ARM64_DBGBCR_E_MASK, ARM64_DBGBCR_E_SHIFT)
#define ARM64_DBGBCR_E_SET(dbgbcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgbcr), (value), ARM64_DBGBCR_E_MASK, \
                                          ARM64_DBGBCR_E_SHIFT)

// PMC, HMC, SSC define the environment where the breakpoint will trigger.
#define ARM64_DBGBCR_PMC 0b11lu  // Bits 1-2.
#define ARM64_DBGBCR_PMC_SHIFT 1u
#define ARM64_DBGBCR_PMC_MASK (ARM64_DBGBCR_PMC << ARM64_DBGBCR_PMC_SHIFT)
#define ARM64_DBGBCR_PMC_GET(dbgbcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgbcr), ARM64_DBGBCR_PMC_MASK, ARM64_DBGBCR_PMC_SHIFT)
#define ARM64_DBGBCR_PMC_SET(dbgbcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgbcr), (value), ARM64_DBGBCR_PMC_MASK, \
                                          ARM64_DBGBCR_PMC_SHIFT)

// Byte Address Select. Defines which half-words triggers the breakpoint.
// In AArch64 implementations (which zircon targets), is res1.
#define ARM64_DBGBCR_BAS 0b1111lu  // Bits 5-8.
#define ARM64_DBGBCR_BAS_SHIFT 5u
#define ARM64_DBGBCR_BAS_MASK (ARM64_DBGBCR_BAS << ARM64_DBGBCR_BAS_SHIFT)
#define ARM64_DBGBCR_BAS_GET(dbgbcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgbcr), ARM64_DBGBCR_BAS_MASK, ARM64_DBGBCR_BAS_SHIFT)
#define ARM64_DBGBCR_BAS_SET(dbgbcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgbcr), (value), ARM64_DBGBCR_BAS_MASK, \
                                          ARM64_DBGBCR_BAS_SHIFT)

// PMC, HMC, SSC define the environment where the breakpoint will trigger.
#define ARM64_DBGBCR_HMC 0b1lu  // Bit 13.
#define ARM64_DBGBCR_HMC_SHIFT 13u
#define ARM64_DBGBCR_HMC_MASK (ARM64_DBGBCR_HMC << ARM64_DBGBCR_HMC_SHIFT)
#define ARM64_DBGBCR_HMC_GET(dbgbcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgbcr), ARM64_DBGBCR_HMC_MASK, ARM64_DBGBCR_HMC_SHIFT)
#define ARM64_DBGBCR_HMC_SET(dbgbcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgbcr), (value), ARM64_DBGBCR_HMC_MASK, \
                                          ARM64_DBGBCR_HMC_SHIFT)

// PMC, HMC, SSC define the environment where the breakpoint will trigger.
#define ARM64_DBGBCR_SSC 0b11lu  // Bits 14-15.
#define ARM64_DBGBCR_SSC_SHIFT 14u
#define ARM64_DBGBCR_SSC_MASK (ARM64_DBGBCR_SSC << ARM64_DBGBCR_SSC_SHIFT)
#define ARM64_DBGBCR_SSC_GET(dbgbcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgbcr), ARM64_DBGBCR_SSC_MASK, ARM64_DBGBCR_SSC_SHIFT)
#define ARM64_DBGBCR_SSC_SET(dbgbcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgbcr), (value), ARM64_DBGBCR_SSC_MASK, \
                                          ARM64_DBGBCR_SSC_SHIFT)

// Linked Breakpoint Number. Zircon doesn't use this feature. Always zero.
#define ARM64_DBGBCR_LBN 0b1111lu  // Bits 16-19.
#define ARM64_DBGBCR_LBN_SHIFT 16u
#define ARM64_DBGBCR_LBN_MASK (ARM64_DBGBCR_LBN << ARM64_DBGBCR_LBN_SHIFT)
#define ARM64_DBGBCR_LBN_GET(dbgbcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgbcr), ARM64_DBGBCR_LBN_MASK, ARM64_DBGBCR_LBN_SHIFT)
#define ARM64_DBGBCR_LBN_SET(dbgbcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgbcr), (value), ARM64_DBGBCR_LBN_MASK, \
                                          ARM64_DBGBCR_LBN_SHIFT)

// Breakpoint Type. Zircon only uses unlinked address match (zero).
#define ARM64_DBGBCR_BT 0b1111lu  // Bits 20-23.
#define ARM64_DBGBCR_BT_SHIFT 20u
#define ARM64_DBGBCR_BT_MASK (ARM64_DBGBCR_BT << ARM64_DBGBCR_BT_SHIFT)
#define ARM64_DBGBCR_BT_GET(dbgbcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgbcr), ARM64_DBGBCR_BT_MASK, ARM64_DBGBCR_BT_SHIFT)
#define ARM64_DBGBCR_BT_SET(dbgbcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgbcr), (value), ARM64_DBGBCR_BT_MASK, \
                                          ARM64_DBGBCR_BT_SHIFT)

// Watchpoints ------------------------------------------------------------------------------------

// Watchpoints permits to stop a thread when it read/writes to a particular address in memory.
// This will work even if the address is read-only memory (for a read, of course).

// ARMv8 assures at least 2 watchpoints.
#define ARM64_MIN_HW_WATCHPOINTS 2
#define ARM64_MAX_HW_WATCHPOINTS 16

// DBGWCR<n>: Watchpoint Control Register.
//
// Control register for watchpoints. There is one for each watchpoint present within the system.
// They go numbering by DBGWCR0, DBGWCR1, ... until the value defined ID_AAFR0_EL1.
// For each control register, there is an equivalent DBGWCR<n> that holds the address the thread
// will compare against. How this address is interpreted depends upon the configuration of the
// associated control register.

// The following fields are accepted by the user. All other fields are ignored (masked).
//
// - E
// - BAS
// - TODO(donosoc): Expose LSC.

// This mask is applied when to DBGWCR. Any set values on those fields will be overwritten.
//
// - PAC = 0b10
// - LSC = 0b10: Write watchpoint. TODO(donosoc): Expose to users so they can define it.
// - HMC = 0
// - SSC = 0b01
// - LBN = 0
// - WT  = 0

// Enable/disable the watchpoint.
#define ARM64_DBGWCR_E 1lu  // Bit 1.
#define ARM64_DBGWCR_E_SHIFT 0u
#define ARM64_DBGWCR_E_MASK (ARM64_DBGWCR_E << ARM64_DBGWCR_E_SHIFT)
#define ARM64_DBGWCR_E_GET(dbgwcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgwcr), ARM64_DBGWCR_E_MASK, ARM64_DBGWCR_E_SHIFT)
#define ARM64_DBGWCR_E_SET(dbgwcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgwcr), (value), ARM64_DBGWCR_E_MASK, \
                                          ARM64_DBGWCR_E_SHIFT)

// PAC, SSC, HMC define the environment where the watchpoint will trigger.
#define ARM64_DBGWCR_PAC 0b11lu  // Bits 1-2.
#define ARM64_DBGWCR_PAC_SHIFT 1u
#define ARM64_DBGWCR_PAC_MASK (ARM64_DBGWCR_PAC << ARM64_DBGWCR_PAC_SHIFT)
#define ARM64_DBGWCR_PAC_GET(dbgwcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgwcr), ARM64_DBGWCR_PAC_MASK, ARM64_DBGWCR_PAC_SHIFT)
#define ARM64_DBGWCR_PAC_SET(dbgwcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgwcr), (value), ARM64_DBGWCR_PAC_MASK, \
                                          ARM64_DBGWCR_PAC_SHIFT)

// Load/Store Control.
//
// On what event the watchpoint trigger:
// 01: Read from address.
// 10: Write to address.
// 11: Read/Write to address.
#define ARM64_DBGWCR_LSC 0b11lu  // Bits 3-4.
#define ARM64_DBGWCR_LSC_SHIFT 3u
#define ARM64_DBGWCR_LSC_MASK (ARM64_DBGWCR_LSC << ARM64_DBGWCR_LSC_SHIFT)
#define ARM64_DBGWCR_LSC_GET(dbgwcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgwcr), ARM64_DBGWCR_LSC_MASK, ARM64_DBGWCR_LSC_SHIFT)
#define ARM64_DBGWCR_LSC_SET(dbgwcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgwcr), (value), ARM64_DBGWCR_LSC_MASK, \
                                          ARM64_DBGWCR_LSC_SHIFT)

// Byte Address Select.
//
// Each bit defines what bytes to match onto:
// 0bxxxx'xxx1: Match DBGWVR<n> + 0
// 0bxxxx'xx1x: Match DBGWVR<n> + 1
// 0bxxxx'x1xx: Match DBGWVR<n> + 2
// 0bxxxx'1xxx: Match DBGWVR<n> + 3
// 0bxxx1'xxxx: Match DBGWVR<n> + 4
// 0bxx1x'xxxx: Match DBGWVR<n> + 5
// 0bx1xx'xxxx: Match DBGWVR<n> + 6
// 0b1xxx'xxxx: Match DBGWVR<n> + 7
#define ARM64_DBGWCR_BAS 0b11111111lu  // Bits 5-12.
#define ARM64_DBGWCR_BAS_SHIFT 5u
#define ARM64_DBGWCR_BAS_MASK (ARM64_DBGWCR_BAS << ARM64_DBGWCR_BAS_SHIFT)
#define ARM64_DBGWCR_BAS_GET(dbgwcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgwcr), ARM64_DBGWCR_BAS_MASK, ARM64_DBGWCR_BAS_SHIFT)
#define ARM64_DBGWCR_BAS_SET(dbgwcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgwcr), (value), ARM64_DBGWCR_BAS_MASK, \
                                          ARM64_DBGWCR_BAS_SHIFT)

// PAC, SSC, HMC define the environment where the watchpoint will trigger.
#define ARM64_DBGWCR_HMC 1lu  // Bit 13.
#define ARM64_DBGWCR_HMC_SHIFT 13u
#define ARM64_DBGWCR_HMC_MASK (ARM64_DBGWCR_HMC << ARM64_DBGWCR_HMC_SHIFT)
#define ARM64_DBGWCR_HMC_GET(dbgwcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgwcr), ARM64_DBGWCR_HMC_MASK, ARM64_DBGWCR_HMC_SHIFT)
#define ARM64_DBGWCR_HMC_SET(dbgwcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgwcr), (value), ARM64_DBGWCR_HMC_MASK, \
                                          ARM64_DBGWCR_HMC_SHIFT)

// PAC, SSC, HMC define the environment where the watchpoint will trigger.
#define ARM64_DBGWCR_SSC 0b11lu  // Bits 14-15.
#define ARM64_DBGWCR_SSC_SHIFT 14u
#define ARM64_DBGWCR_SSC_MASK (ARM64_DBGWCR_SSC << ARM64_DBGWCR_SSC_SHIFT)
#define ARM64_DBGWCR_SSC_GET(dbgwcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgwcr), ARM64_DBGWCR_SSC_MASK, ARM64_DBGWCR_SSC_SHIFT)
#define ARM64_DBGWCR_SSC_SET(dbgwcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgwcr), (value), ARM64_DBGWCR_SSC_MASK, \
                                          ARM64_DBGWCR_SSC_SHIFT)

// Linked Breakpoint Number. Zircon doesn't use this feature. Always zero.
#define ARM64_DBGWCR_LBN 0b1111lu  // Bits 16-19.
#define ARM64_DBGWCR_LBN_SHIFT 16u
#define ARM64_DBGWCR_LBN_MASK (ARM64_DBGWCR_LBN << ARM64_DBGWCR_LBN_SHIFT)
#define ARM64_DBGWCR_LBN_GET(dbgwcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgwcr), ARM64_DBGWCR_LBN_MASK, ARM64_DBGWCR_LBN_SHIFT)
#define ARM64_DBGWCR_LBN_SET(dbgwcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgwcr), (value), ARM64_DBGWCR_LBN_MASK, \
                                          ARM64_DBGWCR_LBN_SHIFT)

// Watchpoint Type. Zircon always use unlinked (0).
#define ARM64_DBGWCR_WT 1lu  // Bit 20.
#define ARM64_DBGWCR_WT_SHIFT 20u
#define ARM64_DBGWCR_WT_MASK (ARM64_DBGWCR_WT << ARM64_DBGWCR_WT_SHIFT)
#define ARM64_DBGWCR_WT_GET(dbgwcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgwcr), ARM64_DBGWCR_WT_MASK, ARM64_DBGWCR_WT_SHIFT)
#define ARM64_DBGWCR_WT_SET(dbgwcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgwcr), (value), ARM64_DBGWCR_WT_MASK, \
                                          ARM64_DBGWCR_WT_SHIFT)

// Mask. How many address bits to mask.
// This permits the watchpoint to track up to 2G worth of addresses.
// TODO(donosoc): Initially the debugger is going for parity with x64, which only permits 8 bytes.
//                Eventually expose the ability to track bigger ranges.
#define ARM64_DBGWCR_MSK 0b11111lu  // Bits 24-28.
#define ARM64_DBGWCR_MSK_SHIFT 24u
#define ARM64_DBGWCR_MSK_MASK (ARM64_DBGWCR_MSK << ARM64_DBGWCR_MSK_SHIFT)
#define ARM64_DBGWCR_MSK_GET(dbgwcr) \
  __arm64_internal_hw_debug_get_reg_value((dbgwcr), ARM64_DBGWCR_MSK_MASK, ARM64_DBGWCR_MSK_SHIFT)
#define ARM64_DBGWCR_MSK_SET(dbgwcr, value)                                         \
  __arm64_internal_hw_debug_set_reg_value((dbgwcr), (value), ARM64_DBGWCR_MSK_MASK, \
                                          ARM64_DBGWCR_MSK_SHIFT)

// Helper functions ================================================================================

inline uint32_t __arm64_internal_hw_debug_get_reg_value(uint32_t reg, uint32_t mask,
                                                        uint32_t shift) {
  return (reg & mask) >> shift;
}

inline void __arm64_internal_hw_debug_set_reg_value(uint32_t* reg, uint32_t value, uint32_t mask,
                                                    uint32_t shift) {
  *reg &= ~mask;
  *reg |= (value << shift) & mask;
}

#endif  // SYSROOT_ZIRCON_HW_DEBUG_ARM64_H_
