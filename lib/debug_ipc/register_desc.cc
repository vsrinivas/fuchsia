// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/register_desc.h"


#include "garnet/lib/debug_ipc/protocol.h"
#include "lib/fxl/logging.h"

namespace debug_ipc {

debug_ipc::RegisterID GetSpecialRegisterID(
    debug_ipc::Arch arch, SpecialRegisterType type) {
  switch (arch) {
    case debug_ipc::Arch::kX64:
      switch (type) {
        case SpecialRegisterType::kNone:
          break;
        case SpecialRegisterType::kIP:
          return debug_ipc::RegisterID::kX64_rip;
        case SpecialRegisterType::kSP:
          return debug_ipc::RegisterID::kX64_rsp;
        case SpecialRegisterType::kBP:
          return debug_ipc::RegisterID::kX64_rbp;
      }
      break;

    case debug_ipc::Arch::kArm64:
      switch (type) {
        case SpecialRegisterType::kNone:
          break;
        case SpecialRegisterType::kIP:
          return debug_ipc::RegisterID::kARMv8_pc;
        case SpecialRegisterType::kSP:
          return debug_ipc::RegisterID::kARMv8_sp;
        case SpecialRegisterType::kBP:
          return debug_ipc::RegisterID::kARMv8_x29;
      }
      break;

    case debug_ipc::Arch::kUnknown:
      break;
  }

  FXL_NOTREACHED();
  return debug_ipc::RegisterID::kUnknown;
}

const char* RegisterIDToString(RegisterID id) {
  switch (id) {
    case RegisterID::kUnknown:
      break;

    // ARMv8
    // -------------------------------------------------------------------

    // General purpose.

    case RegisterID::kARMv8_x0:
      return "x0";
    case RegisterID::kARMv8_x1:
      return "x1";
    case RegisterID::kARMv8_x2:
      return "x2";
    case RegisterID::kARMv8_x3:
      return "x3";
    case RegisterID::kARMv8_x4:
      return "x4";
    case RegisterID::kARMv8_x5:
      return "x5";
    case RegisterID::kARMv8_x6:
      return "x6";
    case RegisterID::kARMv8_x7:
      return "x7";
    case RegisterID::kARMv8_x8:
      return "x8";
    case RegisterID::kARMv8_x9:
      return "x9";
    case RegisterID::kARMv8_x10:
      return "x10";
    case RegisterID::kARMv8_x11:
      return "x11";
    case RegisterID::kARMv8_x12:
      return "x12";
    case RegisterID::kARMv8_x13:
      return "x13";
    case RegisterID::kARMv8_x14:
      return "x14";
    case RegisterID::kARMv8_x15:
      return "x15";
    case RegisterID::kARMv8_x16:
      return "x16";
    case RegisterID::kARMv8_x17:
      return "x17";
    case RegisterID::kARMv8_x18:
      return "x18";
    case RegisterID::kARMv8_x19:
      return "x19";
    case RegisterID::kARMv8_x20:
      return "x20";
    case RegisterID::kARMv8_x21:
      return "x21";
    case RegisterID::kARMv8_x22:
      return "x22";
    case RegisterID::kARMv8_x23:
      return "x23";
    case RegisterID::kARMv8_x24:
      return "x24";
    case RegisterID::kARMv8_x25:
      return "x25";
    case RegisterID::kARMv8_x26:
      return "x26";
    case RegisterID::kARMv8_x27:
      return "x27";
    case RegisterID::kARMv8_x28:
      return "x28";
    case RegisterID::kARMv8_x29:
      return "x29";
    case RegisterID::kARMv8_lr:
      return "lr";
    case RegisterID::kARMv8_sp:
      return "sp";
    case RegisterID::kARMv8_pc:
      return "pc";
    case RegisterID::kARMv8_cpsr:
      return "cpsr";

    // FP (none defined for ARM64).

    // Vector.

    case RegisterID::kARMv8_fpcr:
      return "fpcr";
    case RegisterID::kARMv8_fpsr:
      return "fpsr";

    case RegisterID::kARMv8_v0:
      return "v0";
    case RegisterID::kARMv8_v1:
      return "v1";
    case RegisterID::kARMv8_v2:
      return "v2";
    case RegisterID::kARMv8_v3:
      return "v3";
    case RegisterID::kARMv8_v4:
      return "v4";
    case RegisterID::kARMv8_v5:
      return "v5";
    case RegisterID::kARMv8_v6:
      return "v6";
    case RegisterID::kARMv8_v7:
      return "v7";
    case RegisterID::kARMv8_v8:
      return "v8";
    case RegisterID::kARMv8_v9:
      return "v9";
    case RegisterID::kARMv8_v10:
      return "v10";
    case RegisterID::kARMv8_v11:
      return "v11";
    case RegisterID::kARMv8_v12:
      return "v12";
    case RegisterID::kARMv8_v13:
      return "v13";
    case RegisterID::kARMv8_v14:
      return "v14";
    case RegisterID::kARMv8_v15:
      return "v15";
    case RegisterID::kARMv8_v16:
      return "v16";
    case RegisterID::kARMv8_v17:
      return "v17";
    case RegisterID::kARMv8_v18:
      return "v18";
    case RegisterID::kARMv8_v19:
      return "v19";
    case RegisterID::kARMv8_v20:
      return "v20";
    case RegisterID::kARMv8_v21:
      return "v21";
    case RegisterID::kARMv8_v22:
      return "v22";
    case RegisterID::kARMv8_v23:
      return "v23";
    case RegisterID::kARMv8_v24:
      return "v24";
    case RegisterID::kARMv8_v25:
      return "v25";
    case RegisterID::kARMv8_v26:
      return "v26";
    case RegisterID::kARMv8_v27:
      return "v27";
    case RegisterID::kARMv8_v28:
      return "v28";
    case RegisterID::kARMv8_v29:
      return "v29";
    case RegisterID::kARMv8_v30:
      return "v30";
    case RegisterID::kARMv8_v31:
      return "v31";

    // Debug.

    case RegisterID::kARMv8_id_aa64dfr0_el1:
      return "id_aa64dfr0_el1";
    case RegisterID::kARMv8_mdscr_el1:
      return "mdscr_el1";

    case RegisterID::kARMv8_dbgbcr0_el1:
      return "kARMv8_dbgbcr0_el1";
    case RegisterID::kARMv8_dbgbcr1_el1:
      return "kARMv8_dbgbcr1_el1";
    case RegisterID::kARMv8_dbgbcr2_el1:
      return "kARMv8_dbgbcr2_el1";
    case RegisterID::kARMv8_dbgbcr3_el1:
      return "kARMv8_dbgbcr3_el1";
    case RegisterID::kARMv8_dbgbcr4_el1:
      return "kARMv8_dbgbcr4_el1";
    case RegisterID::kARMv8_dbgbcr5_el1:
      return "kARMv8_dbgbcr5_el1";
    case RegisterID::kARMv8_dbgbcr6_el1:
      return "kARMv8_dbgbcr6_el1";
    case RegisterID::kARMv8_dbgbcr7_el1:
      return "kARMv8_dbgbcr7_el1";
    case RegisterID::kARMv8_dbgbcr8_el1:
      return "kARMv8_dbgbcr8_el1";
    case RegisterID::kARMv8_dbgbcr9_el1:
      return "kARMv8_dbgbcr9_el1";
    case RegisterID::kARMv8_dbgbcr10_el1:
      return "kARMv8_dbgbcr10_el1";
    case RegisterID::kARMv8_dbgbcr11_el1:
      return "kARMv8_dbgbcr11_el1";
    case RegisterID::kARMv8_dbgbcr12_el1:
      return "kARMv8_dbgbcr12_el1";
    case RegisterID::kARMv8_dbgbcr13_el1:
      return "kARMv8_dbgbcr13_el1";
    case RegisterID::kARMv8_dbgbcr14_el1:
      return "kARMv8_dbgbcr14_el1";
    case RegisterID::kARMv8_dbgbcr15_el1:
      return "kARMv8_dbgbcr15_el1";

    case RegisterID::kARMv8_dbgbvr0_el1:
      return "kARMv8_dbgbvr0_el1";
    case RegisterID::kARMv8_dbgbvr1_el1:
      return "kARMv8_dbgbvr1_el1";
    case RegisterID::kARMv8_dbgbvr2_el1:
      return "kARMv8_dbgbvr2_el1";
    case RegisterID::kARMv8_dbgbvr3_el1:
      return "kARMv8_dbgbvr3_el1";
    case RegisterID::kARMv8_dbgbvr4_el1:
      return "kARMv8_dbgbvr4_el1";
    case RegisterID::kARMv8_dbgbvr5_el1:
      return "kARMv8_dbgbvr5_el1";
    case RegisterID::kARMv8_dbgbvr6_el1:
      return "kARMv8_dbgbvr6_el1";
    case RegisterID::kARMv8_dbgbvr7_el1:
      return "kARMv8_dbgbvr7_el1";
    case RegisterID::kARMv8_dbgbvr8_el1:
      return "kARMv8_dbgbvr8_el1";
    case RegisterID::kARMv8_dbgbvr9_el1:
      return "kARMv8_dbgbvr9_el1";
    case RegisterID::kARMv8_dbgbvr10_el1:
      return "kARMv8_dbgbvr10_el1";
    case RegisterID::kARMv8_dbgbvr11_el1:
      return "kARMv8_dbgbvr11_el1";
    case RegisterID::kARMv8_dbgbvr12_el1:
      return "kARMv8_dbgbvr12_el1";
    case RegisterID::kARMv8_dbgbvr13_el1:
      return "kARMv8_dbgbvr13_el1";
    case RegisterID::kARMv8_dbgbvr14_el1:
      return "kARMv8_dbgbvr14_el1";
    case RegisterID::kARMv8_dbgbvr15_el1:
      return "kARMv8_dbgbvr15_el1";

      // x64
      // ---------------------------------------------------------------------

      // General purpose.

    case RegisterID::kX64_rax:
      return "rax";
    case RegisterID::kX64_rbx:
      return "rbx";
    case RegisterID::kX64_rcx:
      return "rcx";
    case RegisterID::kX64_rdx:
      return "rdx";
    case RegisterID::kX64_rsi:
      return "rsi";
    case RegisterID::kX64_rdi:
      return "rdi";
    case RegisterID::kX64_rbp:
      return "rbp";
    case RegisterID::kX64_rsp:
      return "rsp";
    case RegisterID::kX64_r8:
      return "r8";
    case RegisterID::kX64_r9:
      return "r9";
    case RegisterID::kX64_r10:
      return "r10";
    case RegisterID::kX64_r11:
      return "r11";
    case RegisterID::kX64_r12:
      return "r12";
    case RegisterID::kX64_r13:
      return "r13";
    case RegisterID::kX64_r14:
      return "r14";
    case RegisterID::kX64_r15:
      return "r15";
    case RegisterID::kX64_rip:
      return "rip";
    case RegisterID::kX64_rflags:
      return "rflags";

    // FP.

    case RegisterID::kX64_fcw:
      return "fcw";
    case RegisterID::kX64_fsw:
      return "fsw";
    case RegisterID::kX64_ftw:
      return "ftw";
    case RegisterID::kX64_fop:
      return "fop";
    case RegisterID::kX64_fip:
      return "fip";
    case RegisterID::kX64_fdp:
      return "fdp";

    case RegisterID::kX64_st0:
      return "st0";
    case RegisterID::kX64_st1:
      return "st1";
    case RegisterID::kX64_st2:
      return "st2";
    case RegisterID::kX64_st3:
      return "st3";
    case RegisterID::kX64_st4:
      return "st4";
    case RegisterID::kX64_st5:
      return "st5";
    case RegisterID::kX64_st6:
      return "st6";
    case RegisterID::kX64_st7:
      return "st7";

    // Vector.

    case RegisterID::kX64_mxcsr:
      return "mxcsr";

    // SSE/SSE2 (128 bit).
    case RegisterID::kX64_xmm0:
      return "xmm0";
    case RegisterID::kX64_xmm1:
      return "xmm1";
    case RegisterID::kX64_xmm2:
      return "xmm2";
    case RegisterID::kX64_xmm3:
      return "xmm3";
    case RegisterID::kX64_xmm4:
      return "xmm4";
    case RegisterID::kX64_xmm5:
      return "xmm5";
    case RegisterID::kX64_xmm6:
      return "xmm6";
    case RegisterID::kX64_xmm7:
      return "xmm7";
    case RegisterID::kX64_xmm8:
      return "xmm8";
    case RegisterID::kX64_xmm9:
      return "xmm9";
    case RegisterID::kX64_xmm10:
      return "xmm10";
    case RegisterID::kX64_xmm11:
      return "xmm11";
    case RegisterID::kX64_xmm12:
      return "xmm12";
    case RegisterID::kX64_xmm13:
      return "xmm13";
    case RegisterID::kX64_xmm14:
      return "xmm14";
    case RegisterID::kX64_xmm15:
      return "xmm15";

    // AVX (256 bit).
    case RegisterID::kX64_ymm0:
      return "ymm0";
    case RegisterID::kX64_ymm1:
      return "ymm1";
    case RegisterID::kX64_ymm2:
      return "ymm2";
    case RegisterID::kX64_ymm3:
      return "ymm3";
    case RegisterID::kX64_ymm4:
      return "ymm4";
    case RegisterID::kX64_ymm5:
      return "ymm5";
    case RegisterID::kX64_ymm6:
      return "ymm6";
    case RegisterID::kX64_ymm7:
      return "ymm7";
    case RegisterID::kX64_ymm8:
      return "ymm8";
    case RegisterID::kX64_ymm9:
      return "ymm9";
    case RegisterID::kX64_ymm10:
      return "ymm10";
    case RegisterID::kX64_ymm11:
      return "ymm11";
    case RegisterID::kX64_ymm12:
      return "ymm12";
    case RegisterID::kX64_ymm13:
      return "ymm13";
    case RegisterID::kX64_ymm14:
      return "ymm14";
    case RegisterID::kX64_ymm15:
      return "ymm15";

    // TODO(donosoc): Add support for AVX-512 when zircon supports it.

    // Debug.

    case RegisterID::kX64_dr0:
      return "dr0";
    case RegisterID::kX64_dr1:
      return "dr1";
    case RegisterID::kX64_dr2:
      return "dr2";
    case RegisterID::kX64_dr3:
      return "dr3";
    case RegisterID::kX64_dr6:
      return "dr6";
    case RegisterID::kX64_dr7:
      return "dr7";
  }

  FXL_NOTREACHED() << "Unknown register requested: "
                   << static_cast<uint32_t>(id);
  return "";
}

}  // namespace debug_ipc
