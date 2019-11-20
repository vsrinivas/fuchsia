// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_ARCH_X86_H_
#define SRC_DEVELOPER_DEBUG_SHARED_ARCH_X86_H_

#include <stdint.h>

#include <string>

namespace debug_ipc {

// Overall functionality for interpreting x86 specific information. This is defined in debug_ipc
// because both the client and the debug agent need to access this information.

// Macros for obtaining the mask of an x86 flag.
// Usage:
//    FLAG_MASK(Dr7LEN0)
#define _X86_FLAG_MASK(shift, mask) ((uint64_t)((mask) << (shift)))
#define X86_FLAG_MASK(flag) _X86_FLAG_MASK(::debug_ipc::k##flag##Shift, ::debug_ipc::k##flag##Mask)

// Macros for obtaining the value of an x86 flag.
// Usage:
//    FLAG_VALUE(value, RflagsNT)
#define _X86_FLAG_VALUE(value, shift, mask) ((uint8_t)((value >> shift) & mask))
#define X86_FLAG_VALUE(value, flag) \
  _X86_FLAG_VALUE(value, ::debug_ipc::k##flag##Shift, ::debug_ipc::k##flag##Mask)

constexpr uint64_t kRflagsCFShift = 0;  // Carry Flag.
constexpr uint64_t kRflagsCFMask = 0x1;
constexpr uint64_t kRflagsPFShift = 2;  // Parity Flag.
constexpr uint64_t kRflagsPFMask = 0x1;
constexpr uint64_t kRflagsAFShift = 4;  // Aux Carry Flag.
constexpr uint64_t kRflagsAFMask = 0x1;
constexpr uint64_t kRflagsZFShift = 6;  // Zero Flag.
constexpr uint64_t kRflagsZFMask = 0x1;
constexpr uint64_t kRflagsSFShift = 7;  // Sign Flag.
constexpr uint64_t kRflagsSFMask = 0x1;
constexpr uint64_t kRflagsTFShift = 8;  // Trap Flag.
constexpr uint64_t kRflagsTFMask = 0x1;

constexpr uint64_t kRflagsIFShift = 9;  // Interrupt Enable Flag.
constexpr uint64_t kRflagsIFMask = 0x1;
constexpr uint64_t kRflagsDFShift = 10;  // Direction Flag.
constexpr uint64_t kRflagsDFMask = 0x1;
constexpr uint64_t kRflagsOFShift = 11;  // Overflow Flag.
constexpr uint64_t kRflagsOFMask = 0x1;

constexpr uint64_t kRflagsIOPLShift = 12;  // IO Privilege Level.
constexpr uint64_t kRflagsIOPLMask = 0x3;
constexpr uint64_t kRflagsNTShift = 14;  // Nested Task.
constexpr uint64_t kRflagsNTMask = 0x1;
constexpr uint64_t kRflagsRFShift = 16;  // Resume Flag.
constexpr uint64_t kRflagsRFMask = 0x1;
constexpr uint64_t kRflagsVMShift = 17;  // Virtual-8086 Mode.
constexpr uint64_t kRflagsVMMask = 0x1;
constexpr uint64_t kRflagsACShift = 18;  // Alignment Check/ Access Control.
constexpr uint64_t kRflagsACMask = 0x1;
constexpr uint64_t kRflagsVIFShift = 19;  // Virtual Interrupt Flag.
constexpr uint64_t kRflagsVIFMask = 0x1;
constexpr uint64_t kRflagsVIPShift = 20;  // Virtual Interrupt Pending.
constexpr uint64_t kRflagsVIPMask = 0x1;
constexpr uint64_t kRflagsIDShift = 21;  // ID Flag.
constexpr uint64_t kRflagsIDMask = 0x1;

constexpr uint64_t kDR6B0Shift = 0;  // HW Breakpoint 0.
constexpr uint64_t kDR6B0Mask = 0x1;
constexpr uint64_t kDR6B1Shift = 1;  // HW Breakpoint 1.
constexpr uint64_t kDR6B1Mask = 0x1;
constexpr uint64_t kDR6B2Shift = 2;  // HW Breakpoint 2.
constexpr uint64_t kDR6B2Mask = 0x1;
constexpr uint64_t kDR6B3Shift = 3;  // HW Breakpoint 3.
constexpr uint64_t kDR6B3Mask = 0x1;
constexpr uint64_t kDR6BDShift = 13;  // Breakpoint Debug Access Detected.
constexpr uint64_t kDR6BDMask = 0x1;
constexpr uint64_t kDR6BSShift = 14;  // Single Step.
constexpr uint64_t kDR6BSMask = 0x1;
constexpr uint64_t kDR6BTShift = 15;  // Breakpoint Task.
constexpr uint64_t kDR6BTMask = 0x1;

constexpr uint64_t kDR7L0Shift = 0;  // HW Breakpoint 0 enabled.
constexpr uint64_t kDR7L0Mask = 0x1;
constexpr uint64_t kDR7G0Shift = 1;  // Global Breakpoint 0 enabled (not used).
constexpr uint64_t kDR7G0Mask = 0x1;
constexpr uint64_t kDR7L1Shift = 2;  // HW Breakpoint 1 enabled.
constexpr uint64_t kDR7L1Mask = 0x1;
constexpr uint64_t kDR7G1Shift = 3;  // Global Breakpoint 1 enabled (not used).
constexpr uint64_t kDR7G1Mask = 0x1;
constexpr uint64_t kDR7L2Shift = 4;  // HW Breakpoint 2 enabled.
constexpr uint64_t kDR7L2Mask = 0x1;
constexpr uint64_t kDR7G2Shift = 5;  // Global Breakpoint 2 enabled (not used).
constexpr uint64_t kDR7G2Mask = 0x1;
constexpr uint64_t kDR7L3Shift = 6;  // HW Breakpoint 3 enabled.
constexpr uint64_t kDR7L3Mask = 0x1;
constexpr uint64_t kDR7G3Shift = 7;  // Global Breakpoint 3 enabled (not used).
constexpr uint64_t kDR7G3Mask = 0x1;
constexpr uint64_t kDR7LEShift = 8;  // Local Exact enabled (not used).
constexpr uint64_t kDR7LEMask = 0x1;
constexpr uint64_t kDR7GEShift = 9;  // Global Exact enabled (not used).
constexpr uint64_t kDR7GEMask = 0x1;
constexpr uint64_t kDR7GDShift = 13;  // General Detect Enabled.
constexpr uint64_t kDR7GDMask = 0x1;

constexpr uint64_t kDR7RW0Shift = 16;  // Bkpt 0 R/W (which exception to trap).
constexpr uint64_t kDR7RW0Mask = 0x3;
constexpr uint64_t kDR7LEN0Shift = 18;  // Bkpt 0 LEN (len of address to match).
constexpr uint64_t kDR7LEN0Mask = 0x3;
constexpr uint64_t kDR7RW1Shift = 20;  // Bkpt 1 R/W (exception type).
constexpr uint64_t kDR7RW1Mask = 0x3;
constexpr uint64_t kDR7LEN1Shift = 22;  // Bkpt 1 LEN (len of address to match).
constexpr uint64_t kDR7LEN1Mask = 0x3;
constexpr uint64_t kDR7RW2Shift = 24;  // Bkpt 2 R/W (exception type).
constexpr uint64_t kDR7RW2Mask = 0x3;
constexpr uint64_t kDR7LEN2Shift = 26;  // Bkpt 2 LEN (len of address to match).
constexpr uint64_t kDR7LEN2Mask = 0x3;
constexpr uint64_t kDR7RW3Shift = 28;  // Bkpt 3 R/W (exception type).
constexpr uint64_t kDR7RW3Mask = 0x3;
constexpr uint64_t kDR7LEN3Shift = 30;  // Bkpt 3 LEN (len of address to match).
constexpr uint64_t kDR7LEN3Mask = 0x3;

// Debug functions ---------------------------------------------------------------------------------

std::string DR6ToString(uint64_t dr6);

std::string DR7ToString(uint64_t dr7);

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_SHARED_ARCH_X86_H_
