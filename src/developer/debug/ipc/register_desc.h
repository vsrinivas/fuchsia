// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_IPC_REGISTER_DESC_H_
#define SRC_DEVELOPER_DEBUG_IPC_REGISTER_DESC_H_

#include <stdint.h>

#include <optional>
#include <string>

#include "src/lib/containers/cpp/array_view.h"

// Holds constant description values for all the register data for all the
// supported architectures.
// The enum definitions mirror the structs defined in the debug information
// for zircon (see zircon/system/public/zircon/syscalls/debug.h).

namespace debug_ipc {

enum class Arch : uint32_t;        // Forward declaration
enum class RegisterID : uint32_t;  // Forward declaration.

struct Register;

enum class SpecialRegisterType {
  kNone,
  kIP, // Instruction Pointer
  kSP, // Stack Pointer
  kTP  // Thread Pointer
};

struct RegisterInfo {
  RegisterID id;
  std::string name;
  Arch arch;

  // Some registers refer to a subset of another register, e.g. "al" (low byte of "rax") on X86 or
  // "w0" (low 32-bits of "x0") on ARM. This ID will be the larger canonical ID. For registers that
  // are themselves canonical, this will be the same as "id".
  RegisterID canonical_id;

  // When asking for a name-to-register mapping, sometimes they map to a part of a register. For
  // example "al" on x64 is the low 8 bits of rax. These will both be 0 for the "canonical" register
  // record.
  //
  // Currently these both must be a multiple of 8 for GetRegisterData() below.
  int bits = 0;
  int shift = 0;  // How many bits shited to the right is the low bit of the value.

  // DWARF register ID if there is one.
  static constexpr uint32_t kNoDwarfId = 0xffffffff;
  uint32_t dwarf_id = kNoDwarfId;
};

const RegisterInfo* InfoForRegister(RegisterID id);
const RegisterInfo* InfoForRegister(Arch arch, const std::string& name);

const char* RegisterIDToString(debug_ipc::RegisterID);
debug_ipc::RegisterID StringToRegisterID(const std::string&);

// Returns the register ID for the given special register.
debug_ipc::RegisterID GetSpecialRegisterID(debug_ipc::Arch, SpecialRegisterType);

// Returns the special register type for a register ID.
SpecialRegisterType GetSpecialRegisterType(RegisterID id);

// Converts the ID number used by DWARF to our register info. Returns null if not found.
const RegisterInfo* DWARFToRegisterInfo(Arch, uint32_t dwarf_reg_id);

// Find out what arch a register ID belongs to
Arch GetArchForRegisterID(RegisterID);

// Returns true if the given register is a "general" register. General
// registers are sent as part of the unwind frame data. Other registers must
// be requested specially from the target.
bool IsGeneralRegister(RegisterID);

// Gets the data for the given register from the array.
//
// This does two things. It searches for either the requested register or the canonical register.
// If it's a different canonical register (like you're asking for the a 32 bits pseudoregister out
// of a 64 bit register), the relevant bits will be extracted.
//
// If found, the return value will be the range of data within the data owned by |regs|
// corresponding to the requested register. If the source data is truncated, the result will be
// truncated also so it may have less data than expected.
//
// If the register is not found, the returned view will be empty.
containers::array_view<uint8_t> GetRegisterData(const std::vector<Register>& regs, RegisterID id);

// These ranges permit to make transformation from registerID to category and
// make some formal verifications.
constexpr uint32_t kARMv8GeneralBegin = 1000;
constexpr uint32_t kARMv8GeneralEnd = 1099;
constexpr uint32_t kARMv8VectorBegin = 1100;
constexpr uint32_t kARMv8VectorEnd = 1299;
constexpr uint32_t kARMv8DebugBegin = 1300;
constexpr uint32_t kARMv8DebugEnd = 1399;

constexpr uint32_t kX64GeneralBegin = 2000;
constexpr uint32_t kX64GeneralEnd = 2099;
constexpr uint32_t kX64FPBegin = 2100;
constexpr uint32_t kX64FPEnd = 2199;
constexpr uint32_t kX64VectorBegin = 2200;
constexpr uint32_t kX64VectorEnd = 2599;
constexpr uint32_t kX64DebugBegin = 2600;
constexpr uint32_t kX64DebugEnd = 2699;

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
  // This register doesn't exist in ARMv8, but it's used as an abstraction for
  // accessing the PSTATE. It's functionally equivalent to SPSR_EL1.
  kARMv8_cpsr = 1034,

  // General-purpose aliases (low 32-bits).
  kARMv8_w0 = 1035,
  kARMv8_w1 = 1036,
  kARMv8_w2 = 1037,
  kARMv8_w3 = 1038,
  kARMv8_w4 = 1039,
  kARMv8_w5 = 1040,
  kARMv8_w6 = 1041,
  kARMv8_w7 = 1042,
  kARMv8_w8 = 1043,
  kARMv8_w9 = 1044,
  kARMv8_w10 = 1045,
  kARMv8_w11 = 1046,
  kARMv8_w12 = 1047,
  kARMv8_w13 = 1048,
  kARMv8_w14 = 1049,
  kARMv8_w15 = 1050,
  kARMv8_w16 = 1051,
  kARMv8_w17 = 1052,
  kARMv8_w18 = 1053,
  kARMv8_w19 = 1054,
  kARMv8_w20 = 1055,
  kARMv8_w21 = 1056,
  kARMv8_w22 = 1057,
  kARMv8_w23 = 1058,
  kARMv8_w24 = 1059,
  kARMv8_w25 = 1060,
  kARMv8_w26 = 1061,
  kARMv8_w27 = 1062,
  kARMv8_w28 = 1063,
  kARMv8_w29 = 1064,
  kARMv8_w30 = 1065,

  kARMv8_x30 = 1066,  // Alias for "LR" above.

  // Thread Pointer/ID register
  kARMv8_tpidr = 1067,

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

  // Debug.

  kARMv8_id_aa64dfr0_el1 = 1300,  // Debug Feature Register 0.
  kARMv8_mdscr_el1 = 1301,        // Debug System Control Register.

  kARMv8_dbgbcr0_el1 = 1320,
  kARMv8_dbgbcr1_el1 = 1321,
  kARMv8_dbgbcr2_el1 = 1322,
  kARMv8_dbgbcr3_el1 = 1323,
  kARMv8_dbgbcr4_el1 = 1324,
  kARMv8_dbgbcr5_el1 = 1325,
  kARMv8_dbgbcr6_el1 = 1326,
  kARMv8_dbgbcr7_el1 = 1327,
  kARMv8_dbgbcr8_el1 = 1328,
  kARMv8_dbgbcr9_el1 = 1329,
  kARMv8_dbgbcr10_el1 = 1330,
  kARMv8_dbgbcr11_el1 = 1331,
  kARMv8_dbgbcr12_el1 = 1332,
  kARMv8_dbgbcr13_el1 = 1333,
  kARMv8_dbgbcr14_el1 = 1334,
  kARMv8_dbgbcr15_el1 = 1335,

  kARMv8_dbgbvr0_el1 = 1350,
  kARMv8_dbgbvr1_el1 = 1351,
  kARMv8_dbgbvr2_el1 = 1352,
  kARMv8_dbgbvr3_el1 = 1353,
  kARMv8_dbgbvr4_el1 = 1354,
  kARMv8_dbgbvr5_el1 = 1355,
  kARMv8_dbgbvr6_el1 = 1356,
  kARMv8_dbgbvr7_el1 = 1357,
  kARMv8_dbgbvr8_el1 = 1358,
  kARMv8_dbgbvr9_el1 = 1359,
  kARMv8_dbgbvr10_el1 = 1360,
  kARMv8_dbgbvr11_el1 = 1361,
  kARMv8_dbgbvr12_el1 = 1362,
  kARMv8_dbgbvr13_el1 = 1363,
  kARMv8_dbgbvr14_el1 = 1364,
  kARMv8_dbgbvr15_el1 = 1365,

  // TODO(bug 40992) Add ARM64 hardware watchpoint registers here.

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

  // General purpose aliases.

  kX64_ah = 2018,
  kX64_al = 2019,
  kX64_ax = 2020,
  kX64_eax = 2021,

  kX64_bh = 2022,
  kX64_bl = 2023,
  kX64_bx = 2024,
  kX64_ebx = 2025,

  kX64_ch = 2026,
  kX64_cl = 2027,
  kX64_cx = 2028,
  kX64_ecx = 2029,

  kX64_dh = 2030,
  kX64_dl = 2031,
  kX64_dx = 2032,
  kX64_edx = 2033,

  kX64_si = 2034,
  kX64_esi = 2035,

  kX64_di = 2036,
  kX64_edi = 2037,

  // Segment registers
  kX64_fsbase = 2038,
  kX64_gsbase = 2039,

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

  // Although these are technically vector registers, they're aliased on top of the x87 (fp*)
  // registers so must be in the same category.
  kX64_mm0 = 2120,
  kX64_mm1 = 2121,
  kX64_mm2 = 2122,
  kX64_mm3 = 2123,
  kX64_mm4 = 2124,
  kX64_mm5 = 2125,
  kX64_mm6 = 2126,
  kX64_mm7 = 2127,

  // Vector.

  kX64_mxcsr = 2200,  // Control and Status register.

  // SSE/AVX (512 bit, 128- and 256-bit variants will use the low bits of these).
  kX64_zmm0 = 2400,
  kX64_zmm1 = 2401,
  kX64_zmm2 = 2402,
  kX64_zmm3 = 2403,
  kX64_zmm4 = 2404,
  kX64_zmm5 = 2405,
  kX64_zmm6 = 2406,
  kX64_zmm7 = 2407,
  kX64_zmm8 = 2408,
  kX64_zmm9 = 2409,
  kX64_zmm10 = 2410,
  kX64_zmm11 = 2411,
  kX64_zmm12 = 2412,
  kX64_zmm13 = 2413,
  kX64_zmm14 = 2414,
  kX64_zmm15 = 2415,
  kX64_zmm16 = 2416,
  kX64_zmm17 = 2417,
  kX64_zmm18 = 2418,
  kX64_zmm19 = 2419,
  kX64_zmm20 = 2420,
  kX64_zmm21 = 2421,
  kX64_zmm22 = 2422,
  kX64_zmm23 = 2423,
  kX64_zmm24 = 2424,
  kX64_zmm25 = 2425,
  kX64_zmm26 = 2426,
  kX64_zmm27 = 2427,
  kX64_zmm28 = 2428,
  kX64_zmm29 = 2429,
  kX64_zmm30 = 2430,
  kX64_zmm31 = 2431,

  // Vector aliases.
  kX64_xmm0 = 2432,
  kX64_xmm1 = 2433,
  kX64_xmm2 = 2434,
  kX64_xmm3 = 2435,
  kX64_xmm4 = 2436,
  kX64_xmm5 = 2437,
  kX64_xmm6 = 2438,
  kX64_xmm7 = 2439,
  kX64_xmm8 = 2440,
  kX64_xmm9 = 2441,
  kX64_xmm10 = 2442,
  kX64_xmm11 = 2443,
  kX64_xmm12 = 2444,
  kX64_xmm13 = 2445,
  kX64_xmm14 = 2446,
  kX64_xmm15 = 2447,
  kX64_xmm16 = 2448,
  kX64_xmm17 = 2449,
  kX64_xmm18 = 2450,
  kX64_xmm19 = 2451,
  kX64_xmm20 = 2452,
  kX64_xmm21 = 2453,
  kX64_xmm22 = 2454,
  kX64_xmm23 = 2455,
  kX64_xmm24 = 2456,
  kX64_xmm25 = 2457,
  kX64_xmm26 = 2458,
  kX64_xmm27 = 2459,
  kX64_xmm28 = 2460,
  kX64_xmm29 = 2461,
  kX64_xmm30 = 2462,
  kX64_xmm31 = 2463,

  kX64_ymm0 = 2464,
  kX64_ymm1 = 2465,
  kX64_ymm2 = 2466,
  kX64_ymm3 = 2467,
  kX64_ymm4 = 2468,
  kX64_ymm5 = 2469,
  kX64_ymm6 = 2470,
  kX64_ymm7 = 2471,
  kX64_ymm8 = 2472,
  kX64_ymm9 = 2473,
  kX64_ymm10 = 2474,
  kX64_ymm11 = 2475,
  kX64_ymm12 = 2476,
  kX64_ymm13 = 2477,
  kX64_ymm14 = 2478,
  kX64_ymm15 = 2479,
  kX64_ymm16 = 2480,
  kX64_ymm17 = 2481,
  kX64_ymm18 = 2482,
  kX64_ymm19 = 2483,
  kX64_ymm20 = 2484,
  kX64_ymm21 = 2485,
  kX64_ymm22 = 2486,
  kX64_ymm23 = 2487,
  kX64_ymm24 = 2488,
  kX64_ymm25 = 2489,
  kX64_ymm26 = 2490,
  kX64_ymm27 = 2491,
  kX64_ymm28 = 2492,
  kX64_ymm29 = 2493,
  kX64_ymm30 = 2494,
  kX64_ymm31 = 2495,

  // Debug.

  kX64_dr0 = 2600,
  kX64_dr1 = 2601,
  kX64_dr2 = 2602,
  kX64_dr3 = 2603,
  // dr4 is reserved.
  // dr5 is reserved.
  kX64_dr6 = 2606,
  kX64_dr7 = 2607,
};

// Categories --------------------------------------------------------------------------------------

enum class RegisterCategory : uint32_t {
  kNone = 0,
  kGeneral,
  kFloatingPoint,
  kVector,
  kDebug,

  kLast,  // Not an element, for marking the max size.
};

const char* RegisterCategoryToString(RegisterCategory);
RegisterCategory RegisterIDToCategory(RegisterID);

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_IPC_REGISTER_DESC_H_
