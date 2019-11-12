// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_HW_DEBUG_X86_H_
#define SYSROOT_ZIRCON_HW_DEBUG_X86_H_

#include <stdint.h>

// x86/x64 Hardware Debug Resources
// =================================================================================================

// Hardware Breakpoints ----------------------------------------------------------------------------
//
// Hardware breakpoints permits to stop a thread when it executes an address setup in one of the
// hw breakpoints registers. They will work independent whether the address in question is
// read-only or not.

// Access macros:
// All the relevant register fields are exposed through macros.
// For convenience of use, use the get/set macros:
//
// uint64_t X86_<REG>_<FIELD>_GET(uint64_t reg)
// void X86_<REG>_<FIELD>_SET(uint64_t* reg, uint64_t value)
//
// Examples:
// uint64_t rw0 = X86_DBG_CONTROL_RW0_GET(dr7);
// X86_DBG_CONTROL_RW0_SET(&dr7, modified_rw0);

// DR6: Debug Status Register.
//
// This register is updated when the CPU encounters a #DB harware exception. This registers permits
// users to interpret the result of an exception, such as if it was a single-step, hardware
// breakpoint, etc.
//
// No bit is writeable from userspace. All values will be ignored.

// Whether the address-breakpoint register 0 detects an enabled breakpoint condition, as specified
// by the DR7 register. It is cleared to 0 otherwise.
#define X86_DBG_STATUS_B0 1ul
#define X86_DBG_STATUS_B0_SHIFT 0
#define X86_DBG_STATUS_B0_MASK (X86_DBG_STATUS_B0 << X86_DBG_STATUS_B0_SHIFT)
#define X86_DBG_STATUS_B0_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_STATUS_B0_MASK, X86_DBG_STATUS_B0_SHIFT)
#define X86_DBG_STATUS_B0_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_STATUS_B0_MASK, \
                                        X86_DBG_STATUS_B0_SHIFT)

// Whether the address-breakpoint register 1 detects an enabled breakpoint condition, as specified
// by the DR7 register. It is cleared to 0 otherwise.
#define X86_DBG_STATUS_B1 1ul
#define X86_DBG_STATUS_B1_SHIFT 1
#define X86_DBG_STATUS_B1_MASK (X86_DBG_STATUS_B1 << X86_DBG_STATUS_B1_SHIFT)
#define X86_DBG_STATUS_B1_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_STATUS_B1_MASK, X86_DBG_STATUS_B1_SHIFT)
#define X86_DBG_STATUS_B1_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_STATUS_B1_MASK, \
                                        X86_DBG_STATUS_B1_SHIFT)

// Whether the address-breakpoint register 2 detects an enabled breakpoint condition, as specified
// by the DR7 register. It is cleared to 0 otherwise.
#define X86_DBG_STATUS_B2 1ul
#define X86_DBG_STATUS_B2_SHIFT 2
#define X86_DBG_STATUS_B2_MASK (X86_DBG_STATUS_B2 << X86_DBG_STATUS_B2_SHIFT)
#define X86_DBG_STATUS_B2_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_STATUS_B2_MASK, X86_DBG_STATUS_B2_SHIFT)
#define X86_DBG_STATUS_B2_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_STATUS_B2_MASK, \
                                        X86_DBG_STATUS_B2_SHIFT)

// Whether the address-breakpoint register 3 detects an enabled breakpoint condition, as specified
// by the DR7 register. It is cleared to 0 otherwise.
#define X86_DBG_STATUS_B3 1ul
#define X86_DBG_STATUS_B3_SHIFT 3
#define X86_DBG_STATUS_B3_MASK (X86_DBG_STATUS_B3 << X86_DBG_STATUS_B3_SHIFT)
#define X86_DBG_STATUS_B3_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_STATUS_B3_MASK, X86_DBG_STATUS_B3_SHIFT)
#define X86_DBG_STATUS_B3_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_STATUS_B3_MASK, \
                                        X86_DBG_STATUS_B3_SHIFT)

// Whether there were any software accesses to any debug register (DR0, DR7) while the
// general-detect condition was enabled in DR7.
#define X86_DBG_STATUS_BD 1ul
#define X86_DBG_STATUS_BD_SHIFT 13
#define X86_DBG_STATUS_BD_MASK (X86_DBG_STATUS_BD << X86_DBG_STATUS_BD_SHIFT)
#define X86_DBG_STATUS_BD_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_STATUS_BD_MASK, X86_DBG_STATUS_BD_SHIFT)
#define X86_DBG_STATUS_BD_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_STATUS_BD_MASK, \
                                        X86_DBG_STATUS_BD_SHIFT)

// Set to 1 whether the #DB exception occurs as a result of a single-step exception. Single step
// has the highest priority among debug exceptions. Other status bits can be set within the DR6
// register among this bit, so callers should also check for those.
#define X86_DBG_STATUS_BS 1ul
#define X86_DBG_STATUS_BS_SHIFT 14
#define X86_DBG_STATUS_BS_MASK (X86_DBG_STATUS_BS << X86_DBG_STATUS_BS_SHIFT)
#define X86_DBG_STATUS_BS_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_STATUS_BS_MASK, X86_DBG_STATUS_BS_SHIFT)
#define X86_DBG_STATUS_BS_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_STATUS_BS_MASK, \
                                        X86_DBG_STATUS_BS_SHIFT)

// Set to 1 when the exception occurred as a result of a intel task switch to another intel task
// with a TSS T-bit set to 1. This is not used by zircon.
#define X86_DBG_STATUS_BT 1ul
#define X86_DBG_STATUS_BT_SHIFT 15
#define X86_DBG_STATUS_BT_MASK (X86_DBG_STATUS_BT << X86_DBG_STATUS_BT_SHIFT)
#define X86_DBG_STATUS_BT_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_STATUS_BT_MASK, X86_DBG_STATUS_BT_SHIFT)
#define X86_DBG_STATUS_BT_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_STATUS_BT_MASK, \
                                        X86_DBG_STATUS_BT_SHIFT)

// DR7: Debug Control Register.
//
// This register is used to establish the breakpoint conditions for the address breakpoint registers
// (DR0-DR3) and to enable debug exceptions for each of them individually. DR7 is also used to
// enable general-detect breakpoint condition (not permitted by zircon).
//
// The following fields are accepted by the user. All other fields are ignored (masked):
//
// - L0, L1, L2, L3
// - LEN0, LEN1, LEN2, LEN3
// - RW0, RW1, RW2, RW3

// Local Breakpoint Enable 0.
// Enables debug exceptions to occur when the corresponding address register (DR0) detects a
// breakpoint condition on the current intel task. This bit is never cleared by the processor.
#define X86_DBG_CONTROL_L0 1ul
#define X86_DBG_CONTROL_L0_SHIFT 0
#define X86_DBG_CONTROL_L0_MASK (X86_DBG_CONTROL_L0 << X86_DBG_CONTROL_L0_SHIFT)
#define X86_DBG_CONTROL_L0_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_L0_MASK, X86_DBG_CONTROL_L0_SHIFT)
#define X86_DBG_CONTROL_L0_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_L0_MASK, \
                                        X86_DBG_CONTROL_L0_SHIFT)

// Global Breakpoint Enable 0.
// Enables debug exceptions to occur when the corresponding address breakpoint (DR0) detects a
// breakpoint condition while executing *any* intel task. This bit is not cleared by the processor.
// Zircon does not permit to set this bit.
#define X86_DBG_CONTROL_G0 1ul
#define X86_DBG_CONTROL_G0_SHIFT 1
#define X86_DBG_CONTROL_G0_MASK (X86_DBG_CONTROL_G0 << X86_DBG_CONTROL_G0_SHIFT)
#define X86_DBG_CONTROL_G0_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_G0_MASK, X86_DBG_CONTROL_G0_SHIFT)
#define X86_DBG_CONTROL_G0_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_G0_MASK, \
                                        X86_DBG_CONTROL_G0_SHIFT)

// Local Breakpoint Enable 1.
// Enables debug exceptions to occur when the corresponding address register (DR1) detects a
// breakpoint condition on the current intel task. This bit is never cleared by the processor.
#define X86_DBG_CONTROL_L1 1ul
#define X86_DBG_CONTROL_L1_SHIFT 2
#define X86_DBG_CONTROL_L1_MASK (X86_DBG_CONTROL_L1 << X86_DBG_CONTROL_L1_SHIFT)
#define X86_DBG_CONTROL_L1_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_L1_MASK, X86_DBG_CONTROL_L1_SHIFT)
#define X86_DBG_CONTROL_L1_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_L1_MASK, \
                                        X86_DBG_CONTROL_L1_SHIFT)

// Global Breakpoint Enable 1.
// Enables debug exceptions to occur when the corresponding address breakpoint (DR1) detects a
// breakpoint condition while executing *any* intel task. This bit is not cleared by the processor.
// Zircon does not permit to set this bit.
#define X86_DBG_CONTROL_G1 1ul
#define X86_DBG_CONTROL_G1_SHIFT 3
#define X86_DBG_CONTROL_G1_MASK (X86_DBG_CONTROL_G1 << X86_DBG_CONTROL_G1_SHIFT)
#define X86_DBG_CONTROL_G1_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_G1_MASK, X86_DBG_CONTROL_G1_SHIFT)
#define X86_DBG_CONTROL_G1_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_G1_MASK, \
                                        X86_DBG_CONTROL_G1_SHIFT)

// Local Breakpoint Enable 2.
// Enables debug exceptions to occur when the corresponding address register (DR2) detects a
// breakpoint condition on the current intel task. This bit is never cleared by the processor.
#define X86_DBG_CONTROL_L2 1ul
#define X86_DBG_CONTROL_L2_SHIFT 4
#define X86_DBG_CONTROL_L2_MASK (X86_DBG_CONTROL_L2 << X86_DBG_CONTROL_L2_SHIFT)
#define X86_DBG_CONTROL_L2_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_L2_MASK, X86_DBG_CONTROL_L2_SHIFT)
#define X86_DBG_CONTROL_L2_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_L2_MASK, \
                                        X86_DBG_CONTROL_L2_SHIFT)

// Global Breakpoint Enable 2.
// Enables debug exceptions to occur when the corresponding address breakpoint (DR2) detects a
// breakpoint condition while executing *any* intel task. This bit is not cleared by the processor.
// Zircon does not permit to set this bit.
#define X86_DBG_CONTROL_G2 1ul
#define X86_DBG_CONTROL_G2_SHIFT 5
#define X86_DBG_CONTROL_G2_MASK (X86_DBG_CONTROL_G2 << X86_DBG_CONTROL_G2_SHIFT)
#define X86_DBG_CONTROL_G2_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_G2_MASK, X86_DBG_CONTROL_G2_SHIFT)
#define X86_DBG_CONTROL_G2_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_G2_MASK, \
                                        X86_DBG_CONTROL_G2_SHIFT)

// Local Breakpoint Enable 3.
// Enables debug exceptions to occur when the corresponding address register (DR3) detects a
// breakpoint condition on the current intel task. This bit is never cleared by the processor.
#define X86_DBG_CONTROL_L3 1ul
#define X86_DBG_CONTROL_L3_SHIFT 6
#define X86_DBG_CONTROL_L3_MASK (X86_DBG_CONTROL_L3 << X86_DBG_CONTROL_L3_SHIFT)
#define X86_DBG_CONTROL_L3_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_L3_MASK, X86_DBG_CONTROL_L3_SHIFT)
#define X86_DBG_CONTROL_L3_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_L3_MASK, \
                                        X86_DBG_CONTROL_L3_SHIFT)

// Global Breakpoint Enable 3.
// Enables debug exceptions to occur when the corresponding address breakpoint (DR3) detects a
// breakpoint condition while executing *any* intel task. This bit is not cleared by the processor.
// Zircon does not permit to set this bit.
#define X86_DBG_CONTROL_G3 1u
#define X86_DBG_CONTROL_G3_SHIFT 7
#define X86_DBG_CONTROL_G3_MASK (X86_DBG_CONTROL_G3 << X86_DBG_CONTROL_G3_SHIFT)
#define X86_DBG_CONTROL_G3_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_G3_MASK, X86_DBG_CONTROL_G3_SHIFT)
#define X86_DBG_CONTROL_G3_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_G3_MASK, \
                                        X86_DBG_CONTROL_G3_SHIFT)

// Local Enable [Legacy Implementations].
// Enables exact breakpoints on the while executing the current intel task. This bit is ignored by
// implementations of the AMD64 architecture.
// Zircon does not permit to set this bit.
#define X86_DBG_CONTROL_LE 1ul
#define X86_DBG_CONTROL_LE_SHIFT 8
#define X86_DBG_CONTROL_LE_MASK (X86_DBG_CONTROL_LE << X86_DBG_CONTROL_LE_SHIFT)
#define X86_DBG_CONTROL_LE_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_LE_MASK, X86_DBG_CONTROL_LE_SHIFT)
#define X86_DBG_CONTROL_LE_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_LE_MASK, \
                                        X86_DBG_CONTROL_LE_SHIFT)

// Global Enable [Legacy Implementations].
// Enables exact breakpoints on the while executing *any* intel task. This bit is ignored by
// implementations of the AMD64 architecture.
// Zircon does not permit to set this bit.
#define X86_DBG_CONTROL_GE 1ul
#define X86_DBG_CONTROL_GE_SHIFT 9
#define X86_DBG_CONTROL_GE_MASK (X86_DBG_CONTROL_GE << X86_DBG_CONTROL_GE_SHIFT)
#define X86_DBG_CONTROL_GE_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_GE_MASK, X86_DBG_CONTROL_GE_SHIFT)
#define X86_DBG_CONTROL_GE_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_GE_MASK, \
                                        X86_DBG_CONTROL_GE_SHIFT)

// General Detect Enable.
// Whether an attempt to execute a "MOV DR<n>" instruction will trigger a debug exception. This bit
// is cleared when a #DB handler is entered, so the handler can read/write to those registers.
// This exception occurs before executing the instruction and DR6.DB is set the the processor.
// Debuggers can use this bit to prevent the currently executing prgram from interfering with the
// debug operations.
// Zircon does not permit to set this bit.
#define X86_DBG_CONTROL_GD 1ul
#define X86_DBG_CONTROL_GD_SHIFT 13
#define X86_DBG_CONTROL_GD_MASK (X86_DBG_CONTROL_GD << X86_DBG_CONTROL_GD_SHIFT)
#define X86_DBG_CONTROL_GD_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_GD_MASK, X86_DBG_CONTROL_GD_SHIFT)
#define X86_DBG_CONTROL_GD_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_GD_MASK, \
                                        X86_DBG_CONTROL_GD_SHIFT)

// Read/Write 0.
// Controls the breakpoint conditions used by the corresponding address breakpoint register (DR0).
// The values are:
// - 00: Only instruction execution.
// - 01: Only data write.
// - 10: Dependant by CR4.DE. Not supported by Zircon.
//   - CR4.DE = 0: Undefined.
//   - CR4.DE = 1: Only on I/0 read/write.
// - 11: Only on data read/write.
#define X86_DBG_CONTROL_RW0 3ul
#define X86_DBG_CONTROL_RW0_SHIFT 16
#define X86_DBG_CONTROL_RW0_MASK (X86_DBG_CONTROL_RW0 << X86_DBG_CONTROL_RW0_SHIFT)
#define X86_DBG_CONTROL_RW0_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_RW0_MASK, X86_DBG_CONTROL_RW0_SHIFT)
#define X86_DBG_CONTROL_RW0_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_RW0_MASK, \
                                        X86_DBG_CONTROL_RW0_SHIFT)

// Length 0.
// Controls the range used in comparing a memory address with the corresponding address breakpoint
// register (DR0). The values are:
// - 00: 1 byte.
// - 01: 2 byte. DR0 must be 2 byte aligned.
// - 10: 8 byte. DR0 must be 8 byte aligned.
// - 11: 4 byte. DR0 must be 4 byte aligned.
#define X86_DBG_CONTROL_LEN0 3ul
#define X86_DBG_CONTROL_LEN0_SHIFT 18
#define X86_DBG_CONTROL_LEN0_MASK (X86_DBG_CONTROL_LEN0 << X86_DBG_CONTROL_LEN0_SHIFT)
#define X86_DBG_CONTROL_LEN0_GET(reg)                                     \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_LEN0_MASK, \
                                        X86_DBG_CONTROL_LEN0_SHIFT)
#define X86_DBG_CONTROL_LEN0_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_LEN0_MASK, \
                                        X86_DBG_CONTROL_LEN0_SHIFT)

// Read/Write 1.
// Controls the breakpoint conditions used by the corresponding address breakpoint register (DR1).
// The values are:
// - 00: Only instruction execution.
// - 01: Only data write.
// - 10: Dependant by CR4.DE. Not supported by Zircon.
//   - CR4.DE = 0: Undefined.
//   - CR4.DE = 1: Only on I/0 read/write.
// - 11: Only on data read/write.
#define X86_DBG_CONTROL_RW1 3ul
#define X86_DBG_CONTROL_RW1_SHIFT 20
#define X86_DBG_CONTROL_RW1_MASK (X86_DBG_CONTROL_RW1 << X86_DBG_CONTROL_RW1_SHIFT)
#define X86_DBG_CONTROL_RW1_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_RW1_MASK, X86_DBG_CONTROL_RW1_SHIFT)
#define X86_DBG_CONTROL_RW1_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_RW1_MASK, \
                                        X86_DBG_CONTROL_RW1_SHIFT)

// Length 1.
// Controls the range used in comparing a memory address with the corresponding address breakpoint
// register (DR1). The values are:
// - 00: 1 byte.
// - 01: 2 byte. DR0 must be 2 byte aligned.
// - 10: 8 byte. DR0 must be 8 byte aligned.
// - 11: 4 byte. DR0 must be 4 byte aligned.
#define X86_DBG_CONTROL_LEN1 3ul
#define X86_DBG_CONTROL_LEN1_SHIFT 22
#define X86_DBG_CONTROL_LEN1_MASK (X86_DBG_CONTROL_LEN1 << X86_DBG_CONTROL_LEN1_SHIFT)
#define X86_DBG_CONTROL_LEN1_GET(reg)                                     \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_LEN1_MASK, \
                                        X86_DBG_CONTROL_LEN1_SHIFT)
#define X86_DBG_CONTROL_LEN1_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_LEN1_MASK, \
                                        X86_DBG_CONTROL_LEN1_SHIFT)

// Read/Write 2.
// Controls the breakpoint conditions used by the corresponding address breakpoint register (DR2).
// The values are:
// - 00: Only instruction execution.
// - 01: Only data write.
// - 10: Dependant by CR4.DE. Not supported by Zircon.
//   - CR4.DE = 0: Undefined.
//   - CR4.DE = 1: Only on I/0 read/write.
// - 11: Only on data read/write.
#define X86_DBG_CONTROL_RW2 3ul
#define X86_DBG_CONTROL_RW2_SHIFT 24
#define X86_DBG_CONTROL_RW2_MASK (X86_DBG_CONTROL_RW2 << X86_DBG_CONTROL_RW2_SHIFT)
#define X86_DBG_CONTROL_RW2_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_RW2_MASK, X86_DBG_CONTROL_RW2_SHIFT)
#define X86_DBG_CONTROL_RW2_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_RW2_MASK, \
                                        X86_DBG_CONTROL_RW2_SHIFT)

// Length 2.
// Controls the range used in comparing a memory address with the corresponding address breakpoint
// register (DR2). The values are:
// - 00: 1 byte.
// - 01: 2 byte. DR0 must be 2 byte aligned.
// - 10: 8 byte. DR0 must be 8 byte aligned.
// - 11: 4 byte. DR0 must be 4 byte aligned.
#define X86_DBG_CONTROL_LEN2 3ul
#define X86_DBG_CONTROL_LEN2_SHIFT 26
#define X86_DBG_CONTROL_LEN2_MASK (X86_DBG_CONTROL_LEN2 << X86_DBG_CONTROL_LEN2_SHIFT)
#define X86_DBG_CONTROL_LEN2_GET(reg)                                     \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_LEN2_MASK, \
                                        X86_DBG_CONTROL_LEN2_SHIFT)
#define X86_DBG_CONTROL_LEN2_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_LEN2_MASK, \
                                        X86_DBG_CONTROL_LEN2_SHIFT)

// Read/Write 3.
// Controls the breakpoint conditions used by the corresponding address breakpoint register (DR3).
// The values are:
// - 00: Only instruction execution.
// - 01: Only data write.
// - 10: Dependant by CR4.DE. Not supported by Zircon.
//   - CR4.DE = 0: Undefined.
//   - CR4.DE = 1: Only on I/0 read/write.
// - 11: Only on data read/write.
#define X86_DBG_CONTROL_RW3 3ul
#define X86_DBG_CONTROL_RW3_SHIFT 28
#define X86_DBG_CONTROL_RW3_MASK (X86_DBG_CONTROL_RW3 << X86_DBG_CONTROL_RW3_SHIFT)
#define X86_DBG_CONTROL_RW3_GET(reg) \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_RW3_MASK, X86_DBG_CONTROL_RW3_SHIFT)
#define X86_DBG_CONTROL_RW3_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_RW3_MASK, \
                                        X86_DBG_CONTROL_RW3_SHIFT)

// Length 3.
// Controls the range used in comparing a memory address with the corresponding address breakpoint
// register (DR3). The values are:
// - 00: 1 byte.
// - 01: 2 byte. DR0 must be 2 byte aligned.
// - 10: 8 byte. DR0 must be 8 byte aligned.
// - 11: 4 byte. DR0 must be 4 byte aligned.
#define X86_DBG_CONTROL_LEN3 3ul
#define X86_DBG_CONTROL_LEN3_SHIFT 30
#define X86_DBG_CONTROL_LEN3_MASK (X86_DBG_CONTROL_LEN3 << X86_DBG_CONTROL_LEN3_SHIFT)
#define X86_DBG_CONTROL_LEN3_GET(reg)                                     \
  __x86_internal_hw_debug_get_reg_value((reg), X86_DBG_CONTROL_LEN3_MASK, \
                                        X86_DBG_CONTROL_LEN3_SHIFT)
#define X86_DBG_CONTROL_LEN3_SET(reg, value)                                       \
  __x86_internal_hw_debug_set_reg_value((reg), (value), X86_DBG_CONTROL_LEN3_MASK, \
                                        X86_DBG_CONTROL_LEN3_SHIFT)

// Helper functions ================================================================================

inline uint64_t __x86_internal_hw_debug_get_reg_value(uint64_t reg, uint64_t mask, uint64_t shift) {
  return (reg & mask) >> shift;
}

inline void __x86_internal_hw_debug_set_reg_value(uint64_t* reg, uint64_t value, uint64_t mask,
                                                  uint64_t shift) {
  *reg &= ~mask;
  *reg |= (value << shift) & mask;
}

#endif  // SYSROOT_ZIRCON_HW_DEBUG_X86_H_
