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

}  // namespace debug_ipc
