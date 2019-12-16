// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch.h"

namespace debug_agent {
namespace arch {
namespace {

TEST(ArchX64, ReadSegmentRegs) {
  zx_thread_state_general_regs_t regs_in;
  regs_in.fs_base = 0xdeadbeeff00dbabe;
  regs_in.gs_base = 0xabadd00dbeadfeed;
  std::vector<debug_ipc::Register> regs_out;
  ArchProvider::SaveGeneralRegs(regs_in, &regs_out);

  const debug_ipc::Register* fs = nullptr;
  const debug_ipc::Register* gs = nullptr;
  for (const auto& reg : regs_out) {
    if (reg.id == debug_ipc::RegisterID::kX64_fsbase) {
      fs = &reg;
    }

    if (reg.id == debug_ipc::RegisterID::kX64_gsbase) {
      gs = &reg;
    }
  }

  ASSERT_NE(nullptr, fs);
  ASSERT_GE(8u, fs->data.size());
  EXPECT_EQ(8u, fs->data.size());

  EXPECT_EQ(0xbe, fs->data[0]);
  EXPECT_EQ(0xba, fs->data[1]);
  EXPECT_EQ(0x0d, fs->data[2]);
  EXPECT_EQ(0xf0, fs->data[3]);
  EXPECT_EQ(0xef, fs->data[4]);
  EXPECT_EQ(0xbe, fs->data[5]);
  EXPECT_EQ(0xad, fs->data[6]);
  EXPECT_EQ(0xde, fs->data[7]);

  ASSERT_NE(nullptr, gs);
  ASSERT_GE(8u, gs->data.size());
  EXPECT_EQ(8u, gs->data.size());

  EXPECT_EQ(0xed, gs->data[0]);
  EXPECT_EQ(0xfe, gs->data[1]);
  EXPECT_EQ(0xad, gs->data[2]);
  EXPECT_EQ(0xbe, gs->data[3]);
  EXPECT_EQ(0x0d, gs->data[4]);
  EXPECT_EQ(0xd0, gs->data[5]);
  EXPECT_EQ(0xad, gs->data[6]);
  EXPECT_EQ(0xab, gs->data[7]);
}

}  // namespace
}  // namespace arch
}  // namespace debug_agent
