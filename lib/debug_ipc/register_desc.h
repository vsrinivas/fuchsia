// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

// Holds constant description values for all the register data for all the
// supported architectures.
// The enum definitions mirror the structs defined in the debug information
// for zircon (see zircon/system/public/zircon/syscalls/debug.h).

namespace debug_ipc {

enum class RegisterID : uint32_t {
  // ARMv8 ---------------------------------------------------------------------

  ARMv8_x0 = 0,
  ARMv8_x1 = 1,
  ARMv8_x2 = 2,
  ARMv8_x3 = 3,
  ARMv8_x4 = 4,
  ARMv8_x5 = 5,
  ARMv8_x6 = 6,
  ARMv8_x7 = 7,
  ARMv8_x8 = 8,
  ARMv8_x9 = 9,
  ARMv8_x10 = 10,
  ARMv8_x11 = 11,
  ARMv8_x12 = 12,
  ARMv8_x13 = 13,
  ARMv8_x14 = 14,
  ARMv8_x15 = 15,
  ARMv8_x16 = 16,
  ARMv8_x17 = 17,
  ARMv8_x18 = 18,
  ARMv8_x19 = 19,
  ARMv8_x20 = 20,
  ARMv8_x21 = 21,
  ARMv8_x22 = 22,
  ARMv8_x23 = 23,
  ARMv8_x24 = 24,
  ARMv8_x25 = 25,
  ARMv8_x26 = 26,
  ARMv8_x27 = 27,
  ARMv8_x28 = 28,
  ARMv8_x29 = 29,
  ARMv8_lr = 30,
  ARMv8_sp = 31,
  ARMv8_pc = 32,
  ARMv8_cpsr = 34,

  // TODO(donosoc): Add ARMv8 vector registers

  // x64 -----------------------------------------------------------------------

  x64_rax = 1000,
  x64_rbx = 1001,
  x64_rcx = 1002,
  x64_rdx = 1003,
  x64_rsi = 1004,
  x64_rdi = 1005,
  x64_rbp = 1006,
  x64_rsp = 1007,
  x64_r8 = 1008,
  x64_r9 = 1009,
  x64_r10 = 1010,
  x64_r11 = 1011,
  x64_r12 = 1012,
  x64_r13 = 1013,
  x64_r14 = 1014,
  x64_r15 = 1015,
  x64_rip = 1016,
  x64_rflags = 1017,

  // TODO(donosoc): Add x64 vector registers
};

}   // namespace debug_ipc
