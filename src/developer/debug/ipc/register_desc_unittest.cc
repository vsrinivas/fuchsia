// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/register_desc.h"

#include "gtest/gtest.h"
#include "src/developer/debug/ipc/protocol.h"

namespace debug_ipc {

TEST(RegisterDesc, DWARFToRegisterInfo_Arm) {
  EXPECT_EQ(debug_ipc::RegisterID::kARMv8_x0, DWARFToRegisterInfo(Arch::kArm64, 0)->id);
  EXPECT_EQ(debug_ipc::RegisterID::kARMv8_x29, DWARFToRegisterInfo(Arch::kArm64, 29)->id);
  EXPECT_EQ(debug_ipc::RegisterID::kARMv8_lr, DWARFToRegisterInfo(Arch::kArm64, 30)->id);
  EXPECT_EQ(debug_ipc::RegisterID::kARMv8_sp, DWARFToRegisterInfo(Arch::kArm64, 31)->id);

  // DWARF ID 32 is "reserved".
  EXPECT_FALSE(DWARFToRegisterInfo(Arch::kArm64, 32));
}

TEST(RegisterDesc, DWARFToRegisterInfo_x64) {
  // General registers.
  EXPECT_EQ(debug_ipc::RegisterID::kX64_rax, DWARFToRegisterInfo(Arch::kX64, 0)->id);
  EXPECT_EQ(debug_ipc::RegisterID::kX64_rsp, DWARFToRegisterInfo(Arch::kX64, 7)->id);
  EXPECT_EQ(debug_ipc::RegisterID::kX64_r8, DWARFToRegisterInfo(Arch::kX64, 8)->id);
  EXPECT_EQ(debug_ipc::RegisterID::kX64_rflags, DWARFToRegisterInfo(Arch::kX64, 49)->id);

  // xmm registers.
  EXPECT_EQ(debug_ipc::RegisterID::kX64_xmm0, DWARFToRegisterInfo(Arch::kX64, 17)->id);
  EXPECT_EQ(debug_ipc::RegisterID::kX64_xmm15, DWARFToRegisterInfo(Arch::kX64, 32)->id);
  EXPECT_EQ(debug_ipc::RegisterID::kX64_xmm16, DWARFToRegisterInfo(Arch::kX64, 67)->id);
  EXPECT_EQ(debug_ipc::RegisterID::kX64_xmm31, DWARFToRegisterInfo(Arch::kX64, 82)->id);
}

TEST(RegisterIDToCategory, Border) {
  auto IDToCat = RegisterIDToCategory;
  EXPECT_EQ(IDToCat(RegisterID::kUnknown), RegisterCategory::kNone);
  EXPECT_EQ(IDToCat(static_cast<RegisterID>(kARMv8GeneralBegin - 1)), RegisterCategory::kNone);
  EXPECT_EQ(IDToCat(static_cast<RegisterID>(kX64DebugEnd + 1)), RegisterCategory::kNone);
}

TEST(RegisterIDToCategory, ARMv8) {
  auto IDToCat = RegisterIDToCategory;

  // General.
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x0), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x1), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x2), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x3), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x4), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x5), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x6), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x7), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x8), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x9), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x10), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x11), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x12), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x13), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x14), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x15), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x16), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x17), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x18), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x19), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x20), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x21), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x22), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x23), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x24), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x25), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x26), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x27), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x28), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x29), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_x30), RegisterCategory::kGeneral);  // alias for LR
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_lr), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_sp), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_pc), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_cpsr), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_w0), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_w29), RegisterCategory::kGeneral);

  // Vector.
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_fpcr), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_fpsr), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v0), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v1), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v2), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v3), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v4), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v5), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v6), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v7), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v8), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v9), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v10), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v11), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v12), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v13), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v14), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v15), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v16), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v17), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v18), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v19), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v20), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v21), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v22), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v23), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v24), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v25), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v26), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v27), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v28), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v29), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v30), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_v31), RegisterCategory::kVector);

  // Debug.
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_id_aa64dfr0_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_mdscr_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr0_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr1_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr2_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr3_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr4_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr5_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr6_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr7_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr8_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr9_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr10_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr11_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr12_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr13_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr14_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbcr15_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr0_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr1_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr2_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr3_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr4_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr5_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr6_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr7_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr8_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr9_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr10_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr11_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr12_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr13_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr14_el1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kARMv8_dbgbvr15_el1), RegisterCategory::kDebug);
}

TEST(RegisterIDToCategory, x64) {
  auto IDToCat = RegisterIDToCategory;

  // General.
  EXPECT_EQ(IDToCat(RegisterID::kX64_rax), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ah), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_al), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_eax), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ax), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rbx), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rcx), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rdx), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rsi), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rdi), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rbp), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rsp), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r8), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r9), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r10), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r11), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r12), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r13), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r14), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_r15), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rip), RegisterCategory::kGeneral);
  EXPECT_EQ(IDToCat(RegisterID::kX64_rflags), RegisterCategory::kGeneral);

  // Floating Point.
  EXPECT_EQ(IDToCat(RegisterID::kX64_fcw), RegisterCategory::kFloatingPoint);
  EXPECT_EQ(IDToCat(RegisterID::kX64_fsw), RegisterCategory::kFloatingPoint);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ftw), RegisterCategory::kFloatingPoint);
  EXPECT_EQ(IDToCat(RegisterID::kX64_fop), RegisterCategory::kFloatingPoint);
  EXPECT_EQ(IDToCat(RegisterID::kX64_fip), RegisterCategory::kFloatingPoint);
  EXPECT_EQ(IDToCat(RegisterID::kX64_fdp), RegisterCategory::kFloatingPoint);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st0), RegisterCategory::kFloatingPoint);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st1), RegisterCategory::kFloatingPoint);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st2), RegisterCategory::kFloatingPoint);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st3), RegisterCategory::kFloatingPoint);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st4), RegisterCategory::kFloatingPoint);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st5), RegisterCategory::kFloatingPoint);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st6), RegisterCategory::kFloatingPoint);
  EXPECT_EQ(IDToCat(RegisterID::kX64_st7), RegisterCategory::kFloatingPoint);

  EXPECT_EQ(IDToCat(RegisterID::kX64_mm0), RegisterCategory::kFloatingPoint);
  EXPECT_EQ(IDToCat(RegisterID::kX64_mm7), RegisterCategory::kFloatingPoint);

  // Vector.
  EXPECT_EQ(IDToCat(RegisterID::kX64_mxcsr), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm0), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_xmm31), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm0), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_ymm31), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_zmm0), RegisterCategory::kVector);
  EXPECT_EQ(IDToCat(RegisterID::kX64_zmm31), RegisterCategory::kVector);

  // Debug.
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr0), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr1), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr2), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr3), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr6), RegisterCategory::kDebug);
  EXPECT_EQ(IDToCat(RegisterID::kX64_dr7), RegisterCategory::kDebug);
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

  EXPECT_STREQ("id_aa64dfr0", RegisterIDToString(RegisterID::kARMv8_id_aa64dfr0_el1));
  EXPECT_STREQ("mdscr", RegisterIDToString(RegisterID::kARMv8_mdscr_el1));

  EXPECT_STREQ("dbgbcr0", RegisterIDToString(RegisterID::kARMv8_dbgbcr0_el1));
  EXPECT_STREQ("dbgbcr1", RegisterIDToString(RegisterID::kARMv8_dbgbcr1_el1));
  EXPECT_STREQ("dbgbcr2", RegisterIDToString(RegisterID::kARMv8_dbgbcr2_el1));
  EXPECT_STREQ("dbgbcr3", RegisterIDToString(RegisterID::kARMv8_dbgbcr3_el1));
  EXPECT_STREQ("dbgbcr4", RegisterIDToString(RegisterID::kARMv8_dbgbcr4_el1));
  EXPECT_STREQ("dbgbcr5", RegisterIDToString(RegisterID::kARMv8_dbgbcr5_el1));
  EXPECT_STREQ("dbgbcr6", RegisterIDToString(RegisterID::kARMv8_dbgbcr6_el1));
  EXPECT_STREQ("dbgbcr7", RegisterIDToString(RegisterID::kARMv8_dbgbcr7_el1));
  EXPECT_STREQ("dbgbcr8", RegisterIDToString(RegisterID::kARMv8_dbgbcr8_el1));
  EXPECT_STREQ("dbgbcr9", RegisterIDToString(RegisterID::kARMv8_dbgbcr9_el1));
  EXPECT_STREQ("dbgbcr10", RegisterIDToString(RegisterID::kARMv8_dbgbcr10_el1));
  EXPECT_STREQ("dbgbcr11", RegisterIDToString(RegisterID::kARMv8_dbgbcr11_el1));
  EXPECT_STREQ("dbgbcr12", RegisterIDToString(RegisterID::kARMv8_dbgbcr12_el1));
  EXPECT_STREQ("dbgbcr13", RegisterIDToString(RegisterID::kARMv8_dbgbcr13_el1));
  EXPECT_STREQ("dbgbcr14", RegisterIDToString(RegisterID::kARMv8_dbgbcr14_el1));
  EXPECT_STREQ("dbgbcr15", RegisterIDToString(RegisterID::kARMv8_dbgbcr15_el1));

  EXPECT_STREQ("dbgbvr0", RegisterIDToString(RegisterID::kARMv8_dbgbvr0_el1));
  EXPECT_STREQ("dbgbvr1", RegisterIDToString(RegisterID::kARMv8_dbgbvr1_el1));
  EXPECT_STREQ("dbgbvr2", RegisterIDToString(RegisterID::kARMv8_dbgbvr2_el1));
  EXPECT_STREQ("dbgbvr3", RegisterIDToString(RegisterID::kARMv8_dbgbvr3_el1));
  EXPECT_STREQ("dbgbvr4", RegisterIDToString(RegisterID::kARMv8_dbgbvr4_el1));
  EXPECT_STREQ("dbgbvr5", RegisterIDToString(RegisterID::kARMv8_dbgbvr5_el1));
  EXPECT_STREQ("dbgbvr6", RegisterIDToString(RegisterID::kARMv8_dbgbvr6_el1));
  EXPECT_STREQ("dbgbvr7", RegisterIDToString(RegisterID::kARMv8_dbgbvr7_el1));
  EXPECT_STREQ("dbgbvr8", RegisterIDToString(RegisterID::kARMv8_dbgbvr8_el1));
  EXPECT_STREQ("dbgbvr9", RegisterIDToString(RegisterID::kARMv8_dbgbvr9_el1));
  EXPECT_STREQ("dbgbvr10", RegisterIDToString(RegisterID::kARMv8_dbgbvr10_el1));
  EXPECT_STREQ("dbgbvr11", RegisterIDToString(RegisterID::kARMv8_dbgbvr11_el1));
  EXPECT_STREQ("dbgbvr12", RegisterIDToString(RegisterID::kARMv8_dbgbvr12_el1));
  EXPECT_STREQ("dbgbvr13", RegisterIDToString(RegisterID::kARMv8_dbgbvr13_el1));
  EXPECT_STREQ("dbgbvr14", RegisterIDToString(RegisterID::kARMv8_dbgbvr14_el1));
  EXPECT_STREQ("dbgbvr15", RegisterIDToString(RegisterID::kARMv8_dbgbvr15_el1));

  EXPECT_STREQ("dbgwcr0", RegisterIDToString(RegisterID::kARMv8_dbgwcr0_el1));
  EXPECT_STREQ("dbgwcr1", RegisterIDToString(RegisterID::kARMv8_dbgwcr1_el1));
  EXPECT_STREQ("dbgwcr2", RegisterIDToString(RegisterID::kARMv8_dbgwcr2_el1));
  EXPECT_STREQ("dbgwcr3", RegisterIDToString(RegisterID::kARMv8_dbgwcr3_el1));
  EXPECT_STREQ("dbgwcr4", RegisterIDToString(RegisterID::kARMv8_dbgwcr4_el1));
  EXPECT_STREQ("dbgwcr5", RegisterIDToString(RegisterID::kARMv8_dbgwcr5_el1));
  EXPECT_STREQ("dbgwcr6", RegisterIDToString(RegisterID::kARMv8_dbgwcr6_el1));
  EXPECT_STREQ("dbgwcr7", RegisterIDToString(RegisterID::kARMv8_dbgwcr7_el1));
  EXPECT_STREQ("dbgwcr8", RegisterIDToString(RegisterID::kARMv8_dbgwcr8_el1));
  EXPECT_STREQ("dbgwcr9", RegisterIDToString(RegisterID::kARMv8_dbgwcr9_el1));
  EXPECT_STREQ("dbgwcr10", RegisterIDToString(RegisterID::kARMv8_dbgwcr10_el1));
  EXPECT_STREQ("dbgwcr11", RegisterIDToString(RegisterID::kARMv8_dbgwcr11_el1));
  EXPECT_STREQ("dbgwcr12", RegisterIDToString(RegisterID::kARMv8_dbgwcr12_el1));
  EXPECT_STREQ("dbgwcr13", RegisterIDToString(RegisterID::kARMv8_dbgwcr13_el1));
  EXPECT_STREQ("dbgwcr14", RegisterIDToString(RegisterID::kARMv8_dbgwcr14_el1));
  EXPECT_STREQ("dbgwcr15", RegisterIDToString(RegisterID::kARMv8_dbgwcr15_el1));

  EXPECT_STREQ("dbgwvr0", RegisterIDToString(RegisterID::kARMv8_dbgwvr0_el1));
  EXPECT_STREQ("dbgwvr1", RegisterIDToString(RegisterID::kARMv8_dbgwvr1_el1));
  EXPECT_STREQ("dbgwvr2", RegisterIDToString(RegisterID::kARMv8_dbgwvr2_el1));
  EXPECT_STREQ("dbgwvr3", RegisterIDToString(RegisterID::kARMv8_dbgwvr3_el1));
  EXPECT_STREQ("dbgwvr4", RegisterIDToString(RegisterID::kARMv8_dbgwvr4_el1));
  EXPECT_STREQ("dbgwvr5", RegisterIDToString(RegisterID::kARMv8_dbgwvr5_el1));
  EXPECT_STREQ("dbgwvr6", RegisterIDToString(RegisterID::kARMv8_dbgwvr6_el1));
  EXPECT_STREQ("dbgwvr7", RegisterIDToString(RegisterID::kARMv8_dbgwvr7_el1));
  EXPECT_STREQ("dbgwvr8", RegisterIDToString(RegisterID::kARMv8_dbgwvr8_el1));
  EXPECT_STREQ("dbgwvr9", RegisterIDToString(RegisterID::kARMv8_dbgwvr9_el1));
  EXPECT_STREQ("dbgwvr10", RegisterIDToString(RegisterID::kARMv8_dbgwvr10_el1));
  EXPECT_STREQ("dbgwvr11", RegisterIDToString(RegisterID::kARMv8_dbgwvr11_el1));
  EXPECT_STREQ("dbgwvr12", RegisterIDToString(RegisterID::kARMv8_dbgwvr12_el1));
  EXPECT_STREQ("dbgwvr13", RegisterIDToString(RegisterID::kARMv8_dbgwvr13_el1));
  EXPECT_STREQ("dbgwvr14", RegisterIDToString(RegisterID::kARMv8_dbgwvr14_el1));
  EXPECT_STREQ("dbgwvr15", RegisterIDToString(RegisterID::kARMv8_dbgwvr15_el1));

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

TEST(RegisterDesc, GetRegisterData) {
  // Searching in empty list.
  std::vector<Register> register_list;
  EXPECT_TRUE(GetRegisterData(register_list, RegisterID::kX64_rax).empty());

  // Search not found.
  register_list.emplace_back(RegisterID::kX64_rbx,
                             std::vector<uint8_t>{21, 22, 23, 24, 25, 26, 27, 28});
  register_list.emplace_back(RegisterID::kX64_rcx,
                             std::vector<uint8_t>{11, 12, 13, 14, 15, 16, 17, 18});
  EXPECT_TRUE(GetRegisterData(register_list, RegisterID::kX64_rax).empty());

  // Search found, match with canonical.
  register_list.emplace_back(RegisterID::kX64_rax, std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8});
  containers::array_view<uint8_t> found = GetRegisterData(register_list, RegisterID::kX64_rax);
  ASSERT_EQ(8u, found.size());
  EXPECT_EQ(1, found[0]);
  EXPECT_EQ(8, found[7]);

  // Search found, match with non-canonical (32-bit register).
  register_list.emplace_back(RegisterID::kX64_edx, std::vector<uint8_t>{41, 42, 43, 44});
  found = GetRegisterData(register_list, RegisterID::kX64_edx);
  ASSERT_EQ(4u, found.size());
  EXPECT_EQ(41, found[0]);
  EXPECT_EQ(44, found[3]);

  // Search found, match with non-canincal low 32 bits.
  found = GetRegisterData(register_list, RegisterID::kX64_eax);
  ASSERT_EQ(4u, found.size());
  EXPECT_EQ(1, found[0]);
  EXPECT_EQ(4, found[3]);

  // Search found, match with non-canonical non-low bit (requires a shift). "ah" is the 2nd byte.
  found = GetRegisterData(register_list, RegisterID::kX64_ah);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(2, found[0]);
}

}  // namespace debug_ipc
