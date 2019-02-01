// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/records.h"

#include <gtest/gtest.h>

namespace debug_ipc {

using CategoryType = RegisterCategory::Type;

TEST(RegisterIDToCategory, Border) {
  auto IDToCat = RegisterCategory::RegisterIDToCategory;
  EXPECT_EQ(IDToCat(RegisterID::kUnknown), CategoryType::kNone);
  EXPECT_EQ(IDToCat(static_cast<RegisterID>(kARMv8GeneralBegin - 1)),
            CategoryType::kNone);
  EXPECT_EQ(IDToCat(static_cast<RegisterID>(kX64DebugEnd + 1)),
            CategoryType::kNone);
}

TEST(RegisterIDToCategory, ARMv8) {
  auto IDToCat = RegisterCategory::RegisterIDToCategory;

  // General.
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x0), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x1), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x2), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x3), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x4), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x5), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x6), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x7), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x8), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x9), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x10), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x11), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x12), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x13), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x14), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x15), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x16), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x17), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x18), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x19), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x20), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x21), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x22), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x23), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x24), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x25), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x26), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x27), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x28), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x29), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_lr), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_sp), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_pc), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_cpsr), CategoryType::kGeneral);

  // Vector.
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_fpcr), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_fpsr), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v0), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v1), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v2), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v3), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v4), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v5), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v6), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v7), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v8), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v9), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v10), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v11), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v12), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v13), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v14), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v15), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v16), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v17), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v18), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v19), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v20), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v21), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v22), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v23), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v24), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v25), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v26), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v27), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v28), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v29), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v30), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v31), CategoryType::kVector);

  // Debug.
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_id_aa64dfr0_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_mdscr_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr0_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr1_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr2_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr3_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr4_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr5_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr6_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr7_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr8_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr9_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr10_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr11_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr12_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr13_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr14_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr15_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr0_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr1_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr2_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr3_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr4_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr5_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr6_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr7_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr8_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr9_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr10_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr11_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr12_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr13_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr14_el1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr15_el1), CategoryType::kDebug);
}

TEST(RegisterIDToCategory, x64) {
  auto IDToCat = RegisterCategory::RegisterIDToCategory;

  // General.
  EXPECT_EQ(IDToCat(RegisterID::kX64_rax), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rbx), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rcx), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rdx), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rsi), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rdi), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rbp), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rsp), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r8), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r9), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r10), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r11), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r12), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r13), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r14), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r15), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rip), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rflags), CategoryType::kGeneral);

  // Floating Point.
  EXPECT_EQ(IDToCat(RegisterID::kX64_fcw), CategoryType::kFP);
  EXPECT_EQ(IDToCat(RegisterID::kX64_fsw), CategoryType::kFP);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ftw), CategoryType::kFP);
  EXPECT_EQ(IDToCat(RegisterID::kX64_fop), CategoryType::kFP);
  EXPECT_EQ(IDToCat(RegisterID::kX64_fip), CategoryType::kFP);
  EXPECT_EQ(IDToCat(RegisterID::kX64_fdp), CategoryType::kFP);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st0), CategoryType::kFP);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st1), CategoryType::kFP);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st2), CategoryType::kFP);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st3), CategoryType::kFP);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st4), CategoryType::kFP);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st5), CategoryType::kFP);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st6), CategoryType::kFP);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st7), CategoryType::kFP);

  // Vector.
  EXPECT_EQ(IDToCat(RegisterID::kX64_mxcsr), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm0), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm1), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm2), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm3), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm4), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm5), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm6), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm7), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm8), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm9), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm10), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm11), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm12), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm13), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm14), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm15), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm0), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm1), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm2), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm3), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm4), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm5), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm6), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm7), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm8), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm9), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm10), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm11), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm12), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm13), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm14), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm15), CategoryType::kVector);

  // Debug.
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr0), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr2), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr3), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr6), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr7), CategoryType::kDebug);
}


// TODO test

}  // namespace debug_ipc
