// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/records.h"

#include <gtest/gtest.h>

namespace debug_ipc {

using CategoryType = RegisterCategory::Type;

TEST(RegisterIDToCategory, Border) {
  auto IDToCat = RegisterCategory::RegisterIDToCategory;
  EXPECT_EQ(IDToCat(RegisterID::kUnknown), CategoryType::kNone);
  EXPECT_EQ(IDToCat(static_cast<RegisterID>(kARMv8GeneralBegin - 1)), CategoryType::kNone);
  EXPECT_EQ(IDToCat(static_cast<RegisterID>(kX64DebugEnd + 1)), CategoryType::kNone);
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
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x30), CategoryType::kGeneral);  // alias for LR
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_lr), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_sp), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_pc), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_cpsr), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_w0), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_w29), CategoryType::kGeneral);

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
  EXPECT_EQ(IDToCat(RegisterID::kX64_ah), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_al), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_eax), CategoryType::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ax), CategoryType::kGeneral);
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
  EXPECT_EQ(IDToCat(RegisterID::kX64_mm0), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_mm7), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm0), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm31), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm0), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm31), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_zmm0), CategoryType::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_zmm31), CategoryType::kVector);

  // Debug.
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr0), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr1), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr2), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr3), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr6), CategoryType::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr7), CategoryType::kDebug);
}

TEST(RegisterIDToString, Registers) {
  EXPECT_STREQ("x0", RegisterIDToString(RegisterID::kARMv8_x0));
  EXPECT_STREQ("x1", RegisterIDToString(RegisterID::kARMv8_x1));
  EXPECT_STREQ("x2", RegisterIDToString(RegisterID::kARMv8_x2));
  EXPECT_STREQ("x3", RegisterIDToString(RegisterID::kARMv8_x3));
  EXPECT_STREQ("x4", RegisterIDToString(RegisterID::kARMv8_x4));
  EXPECT_STREQ("x5", RegisterIDToString(RegisterID::kARMv8_x5));
  EXPECT_STREQ("x6", RegisterIDToString(RegisterID::kARMv8_x6));
  EXPECT_STREQ("x7", RegisterIDToString(RegisterID::kARMv8_x7));
  EXPECT_STREQ("x8", RegisterIDToString(RegisterID::kARMv8_x8));
  EXPECT_STREQ("x9", RegisterIDToString(RegisterID::kARMv8_x9));
  EXPECT_STREQ("x10", RegisterIDToString(RegisterID::kARMv8_x10));
  EXPECT_STREQ("x11", RegisterIDToString(RegisterID::kARMv8_x11));
  EXPECT_STREQ("x12", RegisterIDToString(RegisterID::kARMv8_x12));
  EXPECT_STREQ("x13", RegisterIDToString(RegisterID::kARMv8_x13));
  EXPECT_STREQ("x14", RegisterIDToString(RegisterID::kARMv8_x14));
  EXPECT_STREQ("x15", RegisterIDToString(RegisterID::kARMv8_x15));
  EXPECT_STREQ("x16", RegisterIDToString(RegisterID::kARMv8_x16));
  EXPECT_STREQ("x17", RegisterIDToString(RegisterID::kARMv8_x17));
  EXPECT_STREQ("x18", RegisterIDToString(RegisterID::kARMv8_x18));
  EXPECT_STREQ("x19", RegisterIDToString(RegisterID::kARMv8_x19));
  EXPECT_STREQ("x20", RegisterIDToString(RegisterID::kARMv8_x20));
  EXPECT_STREQ("x21", RegisterIDToString(RegisterID::kARMv8_x21));
  EXPECT_STREQ("x22", RegisterIDToString(RegisterID::kARMv8_x22));
  EXPECT_STREQ("x23", RegisterIDToString(RegisterID::kARMv8_x23));
  EXPECT_STREQ("x24", RegisterIDToString(RegisterID::kARMv8_x24));
  EXPECT_STREQ("x25", RegisterIDToString(RegisterID::kARMv8_x25));
  EXPECT_STREQ("x26", RegisterIDToString(RegisterID::kARMv8_x26));
  EXPECT_STREQ("x27", RegisterIDToString(RegisterID::kARMv8_x27));
  EXPECT_STREQ("x28", RegisterIDToString(RegisterID::kARMv8_x28));
  EXPECT_STREQ("x29", RegisterIDToString(RegisterID::kARMv8_x29));
  EXPECT_STREQ("lr", RegisterIDToString(RegisterID::kARMv8_lr));
  EXPECT_STREQ("sp", RegisterIDToString(RegisterID::kARMv8_sp));
  EXPECT_STREQ("pc", RegisterIDToString(RegisterID::kARMv8_pc));
  EXPECT_STREQ("cpsr", RegisterIDToString(RegisterID::kARMv8_cpsr));
  EXPECT_STREQ("fpcr", RegisterIDToString(RegisterID::kARMv8_fpcr));
  EXPECT_STREQ("fpsr", RegisterIDToString(RegisterID::kARMv8_fpsr));
  EXPECT_STREQ("v0", RegisterIDToString(RegisterID::kARMv8_v0));
  EXPECT_STREQ("v1", RegisterIDToString(RegisterID::kARMv8_v1));
  EXPECT_STREQ("v2", RegisterIDToString(RegisterID::kARMv8_v2));
  EXPECT_STREQ("v3", RegisterIDToString(RegisterID::kARMv8_v3));
  EXPECT_STREQ("v4", RegisterIDToString(RegisterID::kARMv8_v4));
  EXPECT_STREQ("v5", RegisterIDToString(RegisterID::kARMv8_v5));
  EXPECT_STREQ("v6", RegisterIDToString(RegisterID::kARMv8_v6));
  EXPECT_STREQ("v7", RegisterIDToString(RegisterID::kARMv8_v7));
  EXPECT_STREQ("v8", RegisterIDToString(RegisterID::kARMv8_v8));
  EXPECT_STREQ("v9", RegisterIDToString(RegisterID::kARMv8_v9));
  EXPECT_STREQ("v10", RegisterIDToString(RegisterID::kARMv8_v10));
  EXPECT_STREQ("v11", RegisterIDToString(RegisterID::kARMv8_v11));
  EXPECT_STREQ("v12", RegisterIDToString(RegisterID::kARMv8_v12));
  EXPECT_STREQ("v13", RegisterIDToString(RegisterID::kARMv8_v13));
  EXPECT_STREQ("v14", RegisterIDToString(RegisterID::kARMv8_v14));
  EXPECT_STREQ("v15", RegisterIDToString(RegisterID::kARMv8_v15));
  EXPECT_STREQ("v16", RegisterIDToString(RegisterID::kARMv8_v16));
  EXPECT_STREQ("v17", RegisterIDToString(RegisterID::kARMv8_v17));
  EXPECT_STREQ("v18", RegisterIDToString(RegisterID::kARMv8_v18));
  EXPECT_STREQ("v19", RegisterIDToString(RegisterID::kARMv8_v19));
  EXPECT_STREQ("v20", RegisterIDToString(RegisterID::kARMv8_v20));
  EXPECT_STREQ("v21", RegisterIDToString(RegisterID::kARMv8_v21));
  EXPECT_STREQ("v22", RegisterIDToString(RegisterID::kARMv8_v22));
  EXPECT_STREQ("v23", RegisterIDToString(RegisterID::kARMv8_v23));
  EXPECT_STREQ("v24", RegisterIDToString(RegisterID::kARMv8_v24));
  EXPECT_STREQ("v25", RegisterIDToString(RegisterID::kARMv8_v25));
  EXPECT_STREQ("v26", RegisterIDToString(RegisterID::kARMv8_v26));
  EXPECT_STREQ("v27", RegisterIDToString(RegisterID::kARMv8_v27));
  EXPECT_STREQ("v28", RegisterIDToString(RegisterID::kARMv8_v28));
  EXPECT_STREQ("v29", RegisterIDToString(RegisterID::kARMv8_v29));
  EXPECT_STREQ("v30", RegisterIDToString(RegisterID::kARMv8_v30));
  EXPECT_STREQ("v31", RegisterIDToString(RegisterID::kARMv8_v31));
  EXPECT_STREQ("id_aa64dfr0_el1", RegisterIDToString(RegisterID::kARMv8_id_aa64dfr0_el1));
  EXPECT_STREQ("mdscr_el1", RegisterIDToString(RegisterID::kARMv8_mdscr_el1));
  EXPECT_STREQ("dbgbcr0_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr0_el1));
  EXPECT_STREQ("dbgbcr1_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr1_el1));
  EXPECT_STREQ("dbgbcr2_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr2_el1));
  EXPECT_STREQ("dbgbcr3_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr3_el1));
  EXPECT_STREQ("dbgbcr4_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr4_el1));
  EXPECT_STREQ("dbgbcr5_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr5_el1));
  EXPECT_STREQ("dbgbcr6_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr6_el1));
  EXPECT_STREQ("dbgbcr7_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr7_el1));
  EXPECT_STREQ("dbgbcr8_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr8_el1));
  EXPECT_STREQ("dbgbcr9_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr9_el1));
  EXPECT_STREQ("dbgbcr10_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr10_el1));
  EXPECT_STREQ("dbgbcr11_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr11_el1));
  EXPECT_STREQ("dbgbcr12_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr12_el1));
  EXPECT_STREQ("dbgbcr13_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr13_el1));
  EXPECT_STREQ("dbgbcr14_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr14_el1));
  EXPECT_STREQ("dbgbcr15_el1", RegisterIDToString(RegisterID::kARMv8_dbgbcr15_el1));
  EXPECT_STREQ("dbgbvr0_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr0_el1));
  EXPECT_STREQ("dbgbvr1_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr1_el1));
  EXPECT_STREQ("dbgbvr2_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr2_el1));
  EXPECT_STREQ("dbgbvr3_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr3_el1));
  EXPECT_STREQ("dbgbvr4_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr4_el1));
  EXPECT_STREQ("dbgbvr5_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr5_el1));
  EXPECT_STREQ("dbgbvr6_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr6_el1));
  EXPECT_STREQ("dbgbvr7_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr7_el1));
  EXPECT_STREQ("dbgbvr8_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr8_el1));
  EXPECT_STREQ("dbgbvr9_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr9_el1));
  EXPECT_STREQ("dbgbvr10_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr10_el1));
  EXPECT_STREQ("dbgbvr11_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr11_el1));
  EXPECT_STREQ("dbgbvr12_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr12_el1));
  EXPECT_STREQ("dbgbvr13_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr13_el1));
  EXPECT_STREQ("dbgbvr14_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr14_el1));
  EXPECT_STREQ("dbgbvr15_el1", RegisterIDToString(RegisterID::kARMv8_dbgbvr15_el1));
  EXPECT_STREQ("rax", RegisterIDToString(RegisterID::kX64_rax));
  EXPECT_STREQ("rbx", RegisterIDToString(RegisterID::kX64_rbx));
  EXPECT_STREQ("rcx", RegisterIDToString(RegisterID::kX64_rcx));
  EXPECT_STREQ("rdx", RegisterIDToString(RegisterID::kX64_rdx));
  EXPECT_STREQ("rsi", RegisterIDToString(RegisterID::kX64_rsi));
  EXPECT_STREQ("rdi", RegisterIDToString(RegisterID::kX64_rdi));
  EXPECT_STREQ("rbp", RegisterIDToString(RegisterID::kX64_rbp));
  EXPECT_STREQ("rsp", RegisterIDToString(RegisterID::kX64_rsp));
  EXPECT_STREQ("r8", RegisterIDToString(RegisterID::kX64_r8));
  EXPECT_STREQ("r9", RegisterIDToString(RegisterID::kX64_r9));
  EXPECT_STREQ("r10", RegisterIDToString(RegisterID::kX64_r10));
  EXPECT_STREQ("r11", RegisterIDToString(RegisterID::kX64_r11));
  EXPECT_STREQ("r12", RegisterIDToString(RegisterID::kX64_r12));
  EXPECT_STREQ("r13", RegisterIDToString(RegisterID::kX64_r13));
  EXPECT_STREQ("r14", RegisterIDToString(RegisterID::kX64_r14));
  EXPECT_STREQ("r15", RegisterIDToString(RegisterID::kX64_r15));
  EXPECT_STREQ("rip", RegisterIDToString(RegisterID::kX64_rip));
  EXPECT_STREQ("rflags", RegisterIDToString(RegisterID::kX64_rflags));
  EXPECT_STREQ("fcw", RegisterIDToString(RegisterID::kX64_fcw));
  EXPECT_STREQ("fsw", RegisterIDToString(RegisterID::kX64_fsw));
  EXPECT_STREQ("ftw", RegisterIDToString(RegisterID::kX64_ftw));
  EXPECT_STREQ("fop", RegisterIDToString(RegisterID::kX64_fop));
  EXPECT_STREQ("fip", RegisterIDToString(RegisterID::kX64_fip));
  EXPECT_STREQ("fdp", RegisterIDToString(RegisterID::kX64_fdp));
  EXPECT_STREQ("st0", RegisterIDToString(RegisterID::kX64_st0));
  EXPECT_STREQ("st1", RegisterIDToString(RegisterID::kX64_st1));
  EXPECT_STREQ("st2", RegisterIDToString(RegisterID::kX64_st2));
  EXPECT_STREQ("st3", RegisterIDToString(RegisterID::kX64_st3));
  EXPECT_STREQ("st4", RegisterIDToString(RegisterID::kX64_st4));
  EXPECT_STREQ("st5", RegisterIDToString(RegisterID::kX64_st5));
  EXPECT_STREQ("st6", RegisterIDToString(RegisterID::kX64_st6));
  EXPECT_STREQ("st7", RegisterIDToString(RegisterID::kX64_st7));
  EXPECT_STREQ("mxcsr", RegisterIDToString(RegisterID::kX64_mxcsr));
  EXPECT_STREQ("mm0", RegisterIDToString(RegisterID::kX64_mm0));
  EXPECT_STREQ("mm7", RegisterIDToString(RegisterID::kX64_mm7));
  EXPECT_STREQ("xmm0", RegisterIDToString(RegisterID::kX64_xmm0));
  EXPECT_STREQ("xmm31", RegisterIDToString(RegisterID::kX64_xmm31));
  EXPECT_STREQ("ymm0", RegisterIDToString(RegisterID::kX64_ymm0));
  EXPECT_STREQ("ymm31", RegisterIDToString(RegisterID::kX64_ymm31));
  EXPECT_STREQ("zmm0", RegisterIDToString(RegisterID::kX64_zmm0));
  EXPECT_STREQ("zmm31", RegisterIDToString(RegisterID::kX64_zmm31));
  EXPECT_STREQ("dr0", RegisterIDToString(RegisterID::kX64_dr0));
  EXPECT_STREQ("dr1", RegisterIDToString(RegisterID::kX64_dr1));
  EXPECT_STREQ("dr2", RegisterIDToString(RegisterID::kX64_dr2));
  EXPECT_STREQ("dr3", RegisterIDToString(RegisterID::kX64_dr3));
  EXPECT_STREQ("dr6", RegisterIDToString(RegisterID::kX64_dr6));
  EXPECT_STREQ("dr7", RegisterIDToString(RegisterID::kX64_dr7));
}

}  // namespace debug_ipc
