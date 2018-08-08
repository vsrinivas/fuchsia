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

  // ARMv8 (Range: 1000-1999) --------------------------------------------------

  // General purpose

  kARMv8_x0 = 1000,
  kARMv8_x1 = 1001,
  kARMv8_x2 = 1002,
  kARMv8_x3 = 1003,
  kARMv8_x4 = 1004,
  kARMv8_x5 = 1005,
  kARMv8_x6 = 1006,
  kARMv8_x7 = 1007,
  kARMv8_x8 = 1008,
  kARMv8_x9 = 1009,
  kARMv8_x10 = 1010,
  kARMv8_x11 = 1011,
  kARMv8_x12 = 1012,
  kARMv8_x13 = 1013,
  kARMv8_x14 = 1014,
  kARMv8_x15 = 1015,
  kARMv8_x16 = 1016,
  kARMv8_x17 = 1017,
  kARMv8_x18 = 1018,
  kARMv8_x19 = 1019,
  kARMv8_x20 = 1020,
  kARMv8_x21 = 1021,
  kARMv8_x22 = 1022,
  kARMv8_x23 = 1023,
  kARMv8_x24 = 1024,
  kARMv8_x25 = 1025,
  kARMv8_x26 = 1026,
  kARMv8_x27 = 1027,
  kARMv8_x28 = 1028,
  kARMv8_x29 = 1029,
  kARMv8_lr = 1030,
  kARMv8_sp = 1031,
  kARMv8_pc = 1032,
  kARMv8_cpsr = 1034,

  // FP (None on ARMv8).

  // Vector.

  kARMv8_fpcr = 1100,  // Control register.
  kARMv8_fpsr = 1101,  // Status register.

  kARMv8_v0 = 1200,
  kARMv8_v1 = 1201,
  kARMv8_v2 = 1202,
  kARMv8_v3 = 1203,
  kARMv8_v4 = 1204,
  kARMv8_v5 = 1205,
  kARMv8_v6 = 1206,
  kARMv8_v7 = 1207,
  kARMv8_v8 = 1208,
  kARMv8_v9 = 1209,
  kARMv8_v10 = 1210,
  kARMv8_v11 = 1211,
  kARMv8_v12 = 1212,
  kARMv8_v13 = 1213,
  kARMv8_v14 = 1214,
  kARMv8_v15 = 1215,
  kARMv8_v16 = 1216,
  kARMv8_v17 = 1217,
  kARMv8_v18 = 1218,
  kARMv8_v19 = 1219,
  kARMv8_v20 = 1220,
  kARMv8_v21 = 1221,
  kARMv8_v22 = 1222,
  kARMv8_v23 = 1223,
  kARMv8_v24 = 1224,
  kARMv8_v25 = 1225,
  kARMv8_v26 = 1226,
  kARMv8_v27 = 1227,
  kARMv8_v28 = 1228,
  kARMv8_v29 = 1229,
  kARMv8_v30 = 1230,
  kARMv8_v31 = 1231,

  // x64 (Range: 2000-2999) ----------------------------------------------------

  // General purpose

  kX64_rax = 2000,
  kX64_rbx = 2001,
  kX64_rcx = 2002,
  kX64_rdx = 2003,
  kX64_rsi = 2004,
  kX64_rdi = 2005,
  kX64_rbp = 2006,
  kX64_rsp = 2007,
  kX64_r8 = 2008,
  kX64_r9 = 2009,
  kX64_r10 = 2010,
  kX64_r11 = 2011,
  kX64_r12 = 2012,
  kX64_r13 = 2013,
  kX64_r14 = 2014,
  kX64_r15 = 2015,
  kX64_rip = 2016,
  kX64_rflags = 2017,

  // FP (x87 FPU/MMX).

  kX64_fcw = 2100,  // Control word.
  kX64_fsw = 2101,  // Status word.
  kX64_ftw = 2102,  // Tag word.
                    // 2103 reserved
  kX64_fop = 2104,  // Opcode.
  kX64_fip = 2105,  // Instruction pointer.
  kX64_fdp = 2106,  // Data pointer.

  // The x87/MMX state. For x87 the each "st" entry has the low 80 bits used for
  // the register contents. For MMX, the low 64 bits are used.
  // The higher bits are unused.
  kX64_st0 = 2110,
  kX64_st1 = 2111,
  kX64_st2 = 2112,
  kX64_st3 = 2113,
  kX64_st4 = 2114,
  kX64_st5 = 2115,
  kX64_st6 = 2116,
  kX64_st7 = 2117,

  // Vector.

  kX64_mxcsr = 2200,  // Control and Status register.

  // SSE/SSE2 (128 bit).
  kX64_xmm0 = 2300,
  kX64_xmm1 = 2301,
  kX64_xmm2 = 2302,
  kX64_xmm3 = 2303,
  kX64_xmm4 = 2304,
  kX64_xmm5 = 2305,
  kX64_xmm6 = 2306,
  kX64_xmm7 = 2307,
  kX64_xmm8 = 2308,
  kX64_xmm9 = 2309,
  kX64_xmm10 = 2310,
  kX64_xmm11 = 2311,
  kX64_xmm12 = 2312,
  kX64_xmm13 = 2313,
  kX64_xmm14 = 2314,
  kX64_xmm15 = 2315,

  // AVX (256 bit).
  kX64_ymm0 = 2400,
  kX64_ymm1 = 2401,
  kX64_ymm2 = 2402,
  kX64_ymm3 = 2403,
  kX64_ymm4 = 2404,
  kX64_ymm5 = 2405,
  kX64_ymm6 = 2406,
  kX64_ymm7 = 2407,
  kX64_ymm8 = 2408,
  kX64_ymm9 = 2409,
  kX64_ymm10 = 2410,
  kX64_ymm11 = 2411,
  kX64_ymm12 = 2412,
  kX64_ymm13 = 2413,
  kX64_ymm14 = 2414,
  kX64_ymm15 = 2415,

  // TODO(donosoc): Add AVX-512 support.
};

}  // namespace debug_ipc
