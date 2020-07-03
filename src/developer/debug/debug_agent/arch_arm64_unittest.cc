// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/arch_arm64_helpers_unittest.h"
#include "src/developer/debug/debug_agent/mock_arch_provider.h"
#include "src/developer/debug/debug_agent/test_utils.h"

namespace debug_agent {
namespace arch {
namespace {

TEST(ArchArm64, ReadTPIDR) {
  zx_thread_state_general_regs_t regs_in;
  regs_in.tpidr = 0xdeadbeeff00dbabe;
  std::vector<debug_ipc::Register> regs_out;
  ArchProvider::SaveGeneralRegs(regs_in, &regs_out);

  const debug_ipc::Register* found = nullptr;
  for (const auto& reg : regs_out) {
    if (reg.id == debug_ipc::RegisterID::kARMv8_tpidr) {
      found = &reg;
      break;
    }
  }

  ASSERT_NE(nullptr, found);
  ASSERT_GE(8u, found->data.size());
  EXPECT_EQ(8u, found->data.size());

  EXPECT_EQ(0xbe, found->data[0]);
  EXPECT_EQ(0xba, found->data[1]);
  EXPECT_EQ(0x0d, found->data[2]);
  EXPECT_EQ(0xf0, found->data[3]);
  EXPECT_EQ(0xef, found->data[4]);
  EXPECT_EQ(0xbe, found->data[5]);
  EXPECT_EQ(0xad, found->data[6]);
  EXPECT_EQ(0xde, found->data[7]);
}

}  // namespace
}  // namespace arch
}  // namespace debug_agent
