// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/register_desc.h"

#include "gtest/gtest.h"
#include "src/developer/debug/ipc/protocol.h"

namespace debug_ipc {

TEST(RegisterDesc, DWARFToRegisterID_Arm) {
  EXPECT_EQ(debug_ipc::RegisterID::kARMv8_x0,
            DWARFToRegisterID(Arch::kArm64, 0));
  EXPECT_EQ(debug_ipc::RegisterID::kARMv8_x29,
            DWARFToRegisterID(Arch::kArm64, 29));
  EXPECT_EQ(debug_ipc::RegisterID::kARMv8_lr,
            DWARFToRegisterID(Arch::kArm64, 30));
  EXPECT_EQ(debug_ipc::RegisterID::kARMv8_sp,
            DWARFToRegisterID(Arch::kArm64, 31));

  // DWARF ID 32 is "reserved".
  EXPECT_EQ(debug_ipc::RegisterID::kUnknown,
            DWARFToRegisterID(Arch::kArm64, 32));
}

TEST(RegisterDesc, DWARFToRegisterID_x64) {
  EXPECT_EQ(debug_ipc::RegisterID::kX64_rax,
            DWARFToRegisterID(Arch::kX64, 0));
  EXPECT_EQ(debug_ipc::RegisterID::kX64_rsp,
            DWARFToRegisterID(Arch::kX64, 7));
  EXPECT_EQ(debug_ipc::RegisterID::kX64_r8,
            DWARFToRegisterID(Arch::kX64, 8));
  EXPECT_EQ(debug_ipc::RegisterID::kX64_rflags,
            DWARFToRegisterID(Arch::kX64, 49));
}

}  // namespace debug_ipc
