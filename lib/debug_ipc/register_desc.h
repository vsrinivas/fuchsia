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
  kUnknown = 0,

  // ARMv8 ---------------------------------------------------------------------
  // Range: 1000-1999

  kARMv8_x0   = 1000,
  kARMv8_x1   = 1001,
  kARMv8_x2   = 1002,
  kARMv8_x3   = 1003,
  kARMv8_x4   = 1004,
  kARMv8_x5   = 1005,
  kARMv8_x6   = 1006,
  kARMv8_x7   = 1007,
  kARMv8_x8   = 1008,
  kARMv8_x9   = 1009,
  kARMv8_x10  = 1010,
  kARMv8_x11  = 1011,
  kARMv8_x12  = 1012,
  kARMv8_x13  = 1013,
  kARMv8_x14  = 1014,
  kARMv8_x15  = 1015,
  kARMv8_x16  = 1016,
  kARMv8_x17  = 1017,
  kARMv8_x18  = 1018,
  kARMv8_x19  = 1019,
  kARMv8_x20  = 1020,
  kARMv8_x21  = 1021,
  kARMv8_x22  = 1022,
  kARMv8_x23  = 1023,
  kARMv8_x24  = 1024,
  kARMv8_x25  = 1025,
  kARMv8_x26  = 1026,
  kARMv8_x27  = 1027,
  kARMv8_x28  = 1028,
  kARMv8_x29  = 1029,
  kARMv8_lr   = 1030,
  kARMv8_sp   = 1031,
  kARMv8_pc   = 1032,
  kARMv8_cpsr = 1034,

  // TODO(donosoc): Add ARMv8 vector registers

  // x64 -----------------------------------------------------------------------
  // Range: 2000-2999

  kX64_rax    = 2000,
  kX64_rbx    = 2001,
  kX64_rcx    = 2002,
  kX64_rdx    = 2003,
  kX64_rsi    = 2004,
  kX64_rdi    = 2005,
  kX64_rbp    = 2006,
  kX64_rsp    = 2007,
  kX64_r8     = 2008,
  kX64_r9     = 2009,
  kX64_r10    = 2010,
  kX64_r11    = 2011,
  kX64_r12    = 2012,
  kX64_r13    = 2013,
  kX64_r14    = 2014,
  kX64_r15    = 2015,
  kX64_rip    = 2016,
  kX64_rflags = 2017,

  // TODO(donosoc): Add x64 vector registers
};

}   // namespace debug_ipc
