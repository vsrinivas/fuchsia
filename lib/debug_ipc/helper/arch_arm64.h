// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace debug_ipc {

// Overall functionality for interpreting arm64 specific information.
// This is defined in debug_ipc because both the client and the debug agent
// need to access this information.

// Macros for obtaining the mask of an arm64 flag.
// Usage:
//    FLAG_MASK(CpsrEL)
#define _ARM64_FLAG_MASK(shift, mask) ((uint64_t)((mask) << (shift)))
#define ARM64_FLAG_MASK(flag) \
  _ARM64_FLAG_MASK(::debug_ipc::k##flag##Shift, ::debug_ipc::k##flag##Mask)

// Macros for obtaining the value of an arm64 flag.
// Usage:
//    FLAG_VALUE(value, CpsrV)
#define _ARM64_FLAG_VALUE(value, shift, mask) \
  ((uint8_t)((value >> shift) & mask))
#define ARM64_FLAG_VALUE(value, flag)                   \
  _ARM64_FLAG_VALUE(value, ::debug_ipc::k##flag##Shift, \
                    ::debug_ipc::k##flag##Mask)

constexpr uint64_t kCpsrELShift = 0;  // Exception Level
constexpr uint64_t kCpsrELMask = 0x1;
constexpr uint64_t kCpsrFShift = 6;  // FIQ mask bit.
constexpr uint64_t kCpsrFMask = 0x1;
constexpr uint64_t kCpsrIShift = 7;  // IRQ mask bit.
constexpr uint64_t kCpsrIMask = 0x1;
constexpr uint64_t kCpsrAShift = 8;  // SError mask bit.
constexpr uint64_t kCpsrAMask = 0x1;
constexpr uint64_t kCpsrDShift = 9;  // Debug exception mask bit.
constexpr uint64_t kCpsrDMask = 0x1;
constexpr uint64_t kCpsrILShift = 20;  // Illegal Execution bit.
constexpr uint64_t kCpsrILMask = 0x1;
constexpr uint64_t kCpsrSSShift = 21;  // Single Step.
constexpr uint64_t kCpsrSSMask = 0x1;
constexpr uint64_t kCpsrPANShift = 22;  // Privilege Access Never.
constexpr uint64_t kCpsrPANMask = 0x1;
constexpr uint64_t kCpsrUAOShift = 23;  // Load/Store privilege access.
constexpr uint64_t kCpsrUAOMask = 0x1;

constexpr uint64_t kCpsrVShift = 28;  // Overflow bit.
constexpr uint64_t kCpsrVMask = 0x1;
constexpr uint64_t kCpsrCShift = 29;  // Carry bit.
constexpr uint64_t kCpsrCMask = 0x1;
constexpr uint64_t kCpsrZShift = 30;  // Zero bit.
constexpr uint64_t kCpsrZMask = 0x1;
constexpr uint64_t kCpsrNShift = 31;  // Negative bit.
constexpr uint64_t kCpsrNMask = 0x1;

}  // namespace debug_ipc
