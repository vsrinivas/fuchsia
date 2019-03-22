// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/register_desc.h"

#include <map>

#include "lib/fxl/arraysize.h"
#include "lib/fxl/logging.h"
#include "src/developer/debug/ipc/protocol.h"

namespace debug_ipc {
namespace {

struct RegisterInfo {
  RegisterID id;
  std::string name;
  Arch arch;
};

const RegisterInfo kRegisterInfo[] = {
    // ARMv8
    // -------------------------------------------------------------------

    // General purpose.

    {.id = RegisterID::kARMv8_x0, .name = "x0", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x1, .name = "x1", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x2, .name = "x2", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x3, .name = "x3", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x4, .name = "x4", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x5, .name = "x5", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x6, .name = "x6", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x7, .name = "x7", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x8, .name = "x8", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x9, .name = "x9", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x10, .name = "x10", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x11, .name = "x11", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x12, .name = "x12", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x13, .name = "x13", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x14, .name = "x14", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x15, .name = "x15", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x16, .name = "x16", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x17, .name = "x17", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x18, .name = "x18", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x19, .name = "x19", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x20, .name = "x20", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x21, .name = "x21", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x22, .name = "x22", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x23, .name = "x23", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x24, .name = "x24", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x25, .name = "x25", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x26, .name = "x26", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x27, .name = "x27", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x28, .name = "x28", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_x29, .name = "x29", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_lr, .name = "lr", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_sp, .name = "sp", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_pc, .name = "pc", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_cpsr, .name = "cpsr", .arch = Arch::kArm64},

    // FP (none defined for ARM64).

    // Vector.

    {.id = RegisterID::kARMv8_fpcr, .name = "fpcr", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_fpsr, .name = "fpsr", .arch = Arch::kArm64},

    {.id = RegisterID::kARMv8_v0, .name = "v0", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v1, .name = "v1", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v2, .name = "v2", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v3, .name = "v3", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v4, .name = "v4", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v5, .name = "v5", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v6, .name = "v6", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v7, .name = "v7", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v8, .name = "v8", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v9, .name = "v9", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v10, .name = "v10", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v11, .name = "v11", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v12, .name = "v12", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v13, .name = "v13", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v14, .name = "v14", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v15, .name = "v15", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v16, .name = "v16", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v17, .name = "v17", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v18, .name = "v18", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v19, .name = "v19", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v20, .name = "v20", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v21, .name = "v21", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v22, .name = "v22", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v23, .name = "v23", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v24, .name = "v24", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v25, .name = "v25", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v26, .name = "v26", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v27, .name = "v27", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v28, .name = "v28", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v29, .name = "v29", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v30, .name = "v30", .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_v31, .name = "v31", .arch = Arch::kArm64},

    // Debug.

    {.id = RegisterID::kARMv8_id_aa64dfr0_el1,
     .name = "id_aa64dfr0_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_mdscr_el1,
     .name = "mdscr_el1",
     .arch = Arch::kArm64},

    {.id = RegisterID::kARMv8_dbgbcr0_el1,
     .name = "kARMv8_dbgbcr0_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbcr1_el1,
     .name = "kARMv8_dbgbcr1_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbcr2_el1,
     .name = "kARMv8_dbgbcr2_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbcr3_el1,
     .name = "kARMv8_dbgbcr3_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbcr4_el1,
     .name = "kARMv8_dbgbcr4_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbcr5_el1,
     .name = "kARMv8_dbgbcr5_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbcr6_el1,
     .name = "kARMv8_dbgbcr6_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbcr7_el1,
     .name = "kARMv8_dbgbcr7_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbcr8_el1,
     .name = "kARMv8_dbgbcr8_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbcr9_el1,
     .name = "kARMv8_dbgbcr9_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbcr10_el1,
     .name = "kARMv8_dbgbcr10_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbcr11_el1,
     .name = "kARMv8_dbgbcr11_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbcr12_el1,
     .name = "kARMv8_dbgbcr12_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbcr13_el1,
     .name = "kARMv8_dbgbcr13_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbcr14_el1,
     .name = "kARMv8_dbgbcr14_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbcr15_el1,
     .name = "kARMv8_dbgbcr15_el1",
     .arch = Arch::kArm64},

    {.id = RegisterID::kARMv8_dbgbvr0_el1,
     .name = "kARMv8_dbgbvr0_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbvr1_el1,
     .name = "kARMv8_dbgbvr1_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbvr2_el1,
     .name = "kARMv8_dbgbvr2_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbvr3_el1,
     .name = "kARMv8_dbgbvr3_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbvr4_el1,
     .name = "kARMv8_dbgbvr4_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbvr5_el1,
     .name = "kARMv8_dbgbvr5_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbvr6_el1,
     .name = "kARMv8_dbgbvr6_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbvr7_el1,
     .name = "kARMv8_dbgbvr7_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbvr8_el1,
     .name = "kARMv8_dbgbvr8_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbvr9_el1,
     .name = "kARMv8_dbgbvr9_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbvr10_el1,
     .name = "kARMv8_dbgbvr10_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbvr11_el1,
     .name = "kARMv8_dbgbvr11_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbvr12_el1,
     .name = "kARMv8_dbgbvr12_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbvr13_el1,
     .name = "kARMv8_dbgbvr13_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbvr14_el1,
     .name = "kARMv8_dbgbvr14_el1",
     .arch = Arch::kArm64},
    {.id = RegisterID::kARMv8_dbgbvr15_el1,
     .name = "kARMv8_dbgbvr15_el1",
     .arch = Arch::kArm64},

    // x64
    // ---------------------------------------------------------------------

    // General purpose.

    {.id = RegisterID::kX64_rax, .name = "rax", .arch = Arch::kX64},
    {.id = RegisterID::kX64_rbx, .name = "rbx", .arch = Arch::kX64},
    {.id = RegisterID::kX64_rcx, .name = "rcx", .arch = Arch::kX64},
    {.id = RegisterID::kX64_rdx, .name = "rdx", .arch = Arch::kX64},
    {.id = RegisterID::kX64_rsi, .name = "rsi", .arch = Arch::kX64},
    {.id = RegisterID::kX64_rdi, .name = "rdi", .arch = Arch::kX64},
    {.id = RegisterID::kX64_rbp, .name = "rbp", .arch = Arch::kX64},
    {.id = RegisterID::kX64_rsp, .name = "rsp", .arch = Arch::kX64},
    {.id = RegisterID::kX64_r8, .name = "r8", .arch = Arch::kX64},
    {.id = RegisterID::kX64_r9, .name = "r9", .arch = Arch::kX64},
    {.id = RegisterID::kX64_r10, .name = "r10", .arch = Arch::kX64},
    {.id = RegisterID::kX64_r11, .name = "r11", .arch = Arch::kX64},
    {.id = RegisterID::kX64_r12, .name = "r12", .arch = Arch::kX64},
    {.id = RegisterID::kX64_r13, .name = "r13", .arch = Arch::kX64},
    {.id = RegisterID::kX64_r14, .name = "r14", .arch = Arch::kX64},
    {.id = RegisterID::kX64_r15, .name = "r15", .arch = Arch::kX64},
    {.id = RegisterID::kX64_rip, .name = "rip", .arch = Arch::kX64},
    {.id = RegisterID::kX64_rflags, .name = "rflags", .arch = Arch::kX64},

    // FP.

    {.id = RegisterID::kX64_fcw, .name = "fcw", .arch = Arch::kX64},
    {.id = RegisterID::kX64_fsw, .name = "fsw", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ftw, .name = "ftw", .arch = Arch::kX64},
    {.id = RegisterID::kX64_fop, .name = "fop", .arch = Arch::kX64},
    {.id = RegisterID::kX64_fip, .name = "fip", .arch = Arch::kX64},
    {.id = RegisterID::kX64_fdp, .name = "fdp", .arch = Arch::kX64},

    {.id = RegisterID::kX64_st0, .name = "st0", .arch = Arch::kX64},
    {.id = RegisterID::kX64_st1, .name = "st1", .arch = Arch::kX64},
    {.id = RegisterID::kX64_st2, .name = "st2", .arch = Arch::kX64},
    {.id = RegisterID::kX64_st3, .name = "st3", .arch = Arch::kX64},
    {.id = RegisterID::kX64_st4, .name = "st4", .arch = Arch::kX64},
    {.id = RegisterID::kX64_st5, .name = "st5", .arch = Arch::kX64},
    {.id = RegisterID::kX64_st6, .name = "st6", .arch = Arch::kX64},
    {.id = RegisterID::kX64_st7, .name = "st7", .arch = Arch::kX64},

    // Vector.

    {.id = RegisterID::kX64_mxcsr, .name = "mxcsr", .arch = Arch::kX64},

    // SSE/SSE2 (128 bit).
    {.id = RegisterID::kX64_xmm0, .name = "xmm0", .arch = Arch::kX64},
    {.id = RegisterID::kX64_xmm1, .name = "xmm1", .arch = Arch::kX64},
    {.id = RegisterID::kX64_xmm2, .name = "xmm2", .arch = Arch::kX64},
    {.id = RegisterID::kX64_xmm3, .name = "xmm3", .arch = Arch::kX64},
    {.id = RegisterID::kX64_xmm4, .name = "xmm4", .arch = Arch::kX64},
    {.id = RegisterID::kX64_xmm5, .name = "xmm5", .arch = Arch::kX64},
    {.id = RegisterID::kX64_xmm6, .name = "xmm6", .arch = Arch::kX64},
    {.id = RegisterID::kX64_xmm7, .name = "xmm7", .arch = Arch::kX64},
    {.id = RegisterID::kX64_xmm8, .name = "xmm8", .arch = Arch::kX64},
    {.id = RegisterID::kX64_xmm9, .name = "xmm9", .arch = Arch::kX64},
    {.id = RegisterID::kX64_xmm10, .name = "xmm10", .arch = Arch::kX64},
    {.id = RegisterID::kX64_xmm11, .name = "xmm11", .arch = Arch::kX64},
    {.id = RegisterID::kX64_xmm12, .name = "xmm12", .arch = Arch::kX64},
    {.id = RegisterID::kX64_xmm13, .name = "xmm13", .arch = Arch::kX64},
    {.id = RegisterID::kX64_xmm14, .name = "xmm14", .arch = Arch::kX64},
    {.id = RegisterID::kX64_xmm15, .name = "xmm15", .arch = Arch::kX64},

    // AVX (256 bit).
    {.id = RegisterID::kX64_ymm0, .name = "ymm0", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ymm1, .name = "ymm1", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ymm2, .name = "ymm2", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ymm3, .name = "ymm3", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ymm4, .name = "ymm4", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ymm5, .name = "ymm5", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ymm6, .name = "ymm6", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ymm7, .name = "ymm7", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ymm8, .name = "ymm8", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ymm9, .name = "ymm9", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ymm10, .name = "ymm10", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ymm11, .name = "ymm11", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ymm12, .name = "ymm12", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ymm13, .name = "ymm13", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ymm14, .name = "ymm14", .arch = Arch::kX64},
    {.id = RegisterID::kX64_ymm15, .name = "ymm15", .arch = Arch::kX64},

    // TODO(donosoc): Add support for AVX-512 when zircon supports it.

    // Debug.

    {.id = RegisterID::kX64_dr0, .name = "dr0", .arch = Arch::kX64},
    {.id = RegisterID::kX64_dr1, .name = "dr1", .arch = Arch::kX64},
    {.id = RegisterID::kX64_dr2, .name = "dr2", .arch = Arch::kX64},
    {.id = RegisterID::kX64_dr3, .name = "dr3", .arch = Arch::kX64},
    {.id = RegisterID::kX64_dr6, .name = "dr6", .arch = Arch::kX64},
    {.id = RegisterID::kX64_dr7, .name = "dr7", .arch = Arch::kX64},
};

constexpr size_t kRegisterInfoCount = arraysize(kRegisterInfo);

const RegisterInfo* RegisterIDToInfo(RegisterID id) {
  static std::map<RegisterID, const RegisterInfo*> info_map;

  if (info_map.empty()) {
    for (size_t i = 0; i < kRegisterInfoCount; i++) {
      info_map[kRegisterInfo[i].id] = &kRegisterInfo[i];
    }
  }

  auto iter = info_map.find(id);

  if (iter != info_map.end()) {
    return iter->second;
  }

  return nullptr;
}

RegisterID DWARFToRegisterIDX64(uint32_t dwarf_reg_id) {
  // TODO: It's possibly more maintainable to capture DWARF IDs in the table
  // and just have a cached lookup rather than this switch. We wouldn't even
  // need the arch-specific functions.

  // https://software.intel.com/sites/default/files/article/402129/mpx-linux64-abi.pdf
  // Page 62
  switch (dwarf_reg_id) {
    case 0:
      return RegisterID::kX64_rax;
    case 1:
      return RegisterID::kX64_rdx;
    case 2:
      return RegisterID::kX64_rcx;
    case 3:
      return RegisterID::kX64_rbx;
    case 4:
      return RegisterID::kX64_rsi;
    case 5:
      return RegisterID::kX64_rdi;
    case 6:
      return RegisterID::kX64_rbp;
    case 7:
      return RegisterID::kX64_rsp;
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15: {
      // 8 = r8, 15 = r15
      auto base = static_cast<uint32_t>(RegisterID::kX64_r8);
      return static_cast<RegisterID>(base - 8 + dwarf_reg_id);
    }
    // TODO(donosoc): 17-24 -> %xmm0 - %xmm7
    // TODO(donosoc): 25-32 -> %xmm8 - %xmm15
    // TODO(donosoc): 33-40 -> %st0 - %st7
    // TODO(donosoc): 41-48 -> %mm0 - %mm7
    case 49:
      return RegisterID::kX64_rflags;
      // TODO(donosoc): 50-55 -> (%es, %cs, %ss, %ds, %fs, %gs)
      // 56-57: Reserved
      // TODO(donsooc): 58 -> FS Base Address
      // TODO(donosoc): 59 -> GS Base Address
      // 60-61: Reserved
      // TODO(donosoc): 62 -> %ts (Task Register)
      // TODO(donosoc): 63 -> %ldtr
      // TODO(donosoc): 64 -> %mxcsr (128-bit Media Control and Status)
      // TODO(donosoc): 65 -> %fcw (x87 Control Word)
      // TODO(donosoc): 66 -> %fsw (x87 Status Word)
      // TODO(donosoc): 67-82 -> %xmm16–%xmm31 (Upper Vector Registers 16–31)
      // 83-117: Reserved
      // TODO(donosoc):118-125 -> %k0–%k7 (Vector Mask Registers 0–7)
      // TODO(donosoc):126-129 -> %bnd0–%bnd3 (Bound Registers 0–3)
  }

  return RegisterID::kUnknown;
}

RegisterID DWARFToRegisterIDARMv8(uint32_t dwarf_reg_id) {
  // TODO: See comment in DWARFToRegisterIDX64 about getting rid of these and
  // just adding the info to the table.

  // http://infocenter.arm.com/help/topic/com.arm.doc.ecm0665627/abi_sve_aadwarf_100985_0000_00_en.pdf
  // Page 6
  if (dwarf_reg_id <= 29) {
    auto base = static_cast<uint32_t>(RegisterID::kARMv8_x0);
    return static_cast<RegisterID>(base + dwarf_reg_id);
  }

  switch (dwarf_reg_id) {
    case 32:
      return RegisterID::kARMv8_sp;
      // 31: Reserved
      // TODO(donosoc): 33 -> ELR_mode
      // 34-45: Reserved
      // TODO(donosoc): 46 -> VG 64-bit SVE Vector granule pseudo register
      // TODO(donosoc): 47 -> FFR VG´8-bit SVE first fault register
      // TODO(donosoc): 48-63 -> P0-P15 VG´8-bit SVE predicate registers
      // TODO(donosoc): 64-95 -> V0-V31 128-bit FP/Advanced SIMD registers
      // TODO(donosoc): 96-127 -> Z0-Z31 VG´64-bit SVE vector registers
  }

  return RegisterID::kUnknown;
}

}  // namespace

RegisterID GetSpecialRegisterID(Arch arch, SpecialRegisterType type) {
  switch (arch) {
    case Arch::kX64:
      switch (type) {
        case SpecialRegisterType::kNone:
          break;
        case SpecialRegisterType::kIP:
          return RegisterID::kX64_rip;
        case SpecialRegisterType::kSP:
          return RegisterID::kX64_rsp;
        case SpecialRegisterType::kBP:
          return RegisterID::kX64_rbp;
      }
      break;

    case Arch::kArm64:
      switch (type) {
        case SpecialRegisterType::kNone:
          break;
        case SpecialRegisterType::kIP:
          return RegisterID::kARMv8_pc;
        case SpecialRegisterType::kSP:
          return RegisterID::kARMv8_sp;
        case SpecialRegisterType::kBP:
          return RegisterID::kARMv8_x29;
      }
      break;

    case Arch::kUnknown:
      break;
  }

  FXL_NOTREACHED();
  return RegisterID::kUnknown;
}

const char* RegisterIDToString(RegisterID id) {
  auto info = RegisterIDToInfo(id);

  if (!info) {
    FXL_NOTREACHED() << "Unknown register requested: "
                     << static_cast<uint32_t>(id);
    return "";
  }

  return info->name.c_str();
}

RegisterID StringToRegisterID(const std::string& reg) {
  static std::map<std::string, RegisterID> id_map;

  if (id_map.empty()) {
    // We populate the whole map at once, otherwise any time we try to look up
    // an invalid register ID (fairly often) we have to search the whole list.
    for (size_t i = 0; i < kRegisterInfoCount; i++) {
      id_map[kRegisterInfo[i].name] = kRegisterInfo[i].id;
    }
  }

  auto iter = id_map.find(reg);

  if (iter == id_map.end()) {
    return RegisterID::kUnknown;
  }

  return iter->second;
}

Arch GetArchForRegisterID(RegisterID id) {
  auto info = RegisterIDToInfo(id);

  if (!info) {
    FXL_NOTREACHED() << "Arch for unknown register requested: "
                     << static_cast<uint32_t>(id);
    return Arch::kUnknown;
  }

  return info->arch;
}

SpecialRegisterType GetSpecialRegisterType(RegisterID id) {
  switch (id) {
    case RegisterID::kX64_rip:
    case RegisterID::kARMv8_pc:
      return debug_ipc::SpecialRegisterType::kIP;
    case RegisterID::kX64_rsp:
    case RegisterID::kARMv8_sp:
      return debug_ipc::SpecialRegisterType::kSP;
    case RegisterID::kX64_rbp:
    case RegisterID::kARMv8_x29:
      return debug_ipc::SpecialRegisterType::kBP;
    default:
      return debug_ipc::SpecialRegisterType::kNone;
  }
}

RegisterID DWARFToRegisterID(Arch arch, uint32_t dwarf_reg_id) {
  switch (arch) {
    case Arch::kX64:
      return DWARFToRegisterIDX64(dwarf_reg_id);
    case Arch::kArm64:
      return DWARFToRegisterIDARMv8(dwarf_reg_id);
    case Arch::kUnknown:
      FXL_NOTREACHED() << "Architecture should be known for DWARF mapping.";
      return RegisterID::kUnknown;
  }
}

}  // namespace debug_ipc
