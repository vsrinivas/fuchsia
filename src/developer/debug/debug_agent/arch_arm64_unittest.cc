// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/test_utils.h"
#include "src/developer/debug/ipc/register_test_support.h"
#include "src/developer/debug/shared/logging/file_line_function.h"
#include "src/developer/debug/shared/zx_status.h"

namespace debug_agent {
namespace arch {

TEST(ArchArm64, WriteGeneralRegs) {
  std::vector<debug_ipc::Register> regs;
  regs.push_back(CreateRegisterWithTestData(debug_ipc::RegisterID::kARMv8_x0, 8));
  regs.push_back(CreateRegisterWithTestData(debug_ipc::RegisterID::kARMv8_x3, 8));
  regs.push_back(CreateRegisterWithTestData(debug_ipc::RegisterID::kARMv8_lr, 8));
  regs.push_back(CreateRegisterWithTestData(debug_ipc::RegisterID::kARMv8_pc, 8));

  zx_thread_state_general_regs_t out = {};
  zx_status_t res = WriteGeneralRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.r[0], 0x0102030405060708u);
  EXPECT_EQ(out.r[1], 0u);
  EXPECT_EQ(out.r[2], 0u);
  EXPECT_EQ(out.r[3], 0x0102030405060708u);
  EXPECT_EQ(out.r[4], 0u);
  EXPECT_EQ(out.r[29], 0u);
  EXPECT_EQ(out.lr, 0x0102030405060708u);
  EXPECT_EQ(out.pc, 0x0102030405060708u);

  regs.clear();
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_x0, static_cast<uint64_t>(0xaabb));
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_x15, static_cast<uint64_t>(0xdead));
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_pc, static_cast<uint64_t>(0xbeef));

  res = WriteGeneralRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.r[0], 0xaabbu);
  EXPECT_EQ(out.r[1], 0u);
  EXPECT_EQ(out.r[15], 0xdeadu);
  EXPECT_EQ(out.r[29], 0u);
  EXPECT_EQ(out.lr, 0x0102030405060708u);
  EXPECT_EQ(out.pc, 0xbeefu);
}

TEST(ArchArm64, InvalidWriteGeneralRegs) {
  zx_thread_state_general_regs_t out;
  std::vector<debug_ipc::Register> regs;

  // Invalid length.
  regs.push_back(CreateRegisterWithTestData(debug_ipc::RegisterID::kARMv8_v0, 4));
  EXPECT_EQ(WriteGeneralRegisters(regs, &out), ZX_ERR_INVALID_ARGS);

  // Invalid (non-canonical) register.
  regs.push_back(CreateRegisterWithTestData(debug_ipc::RegisterID::kARMv8_w3, 8));
  EXPECT_EQ(WriteGeneralRegisters(regs, &out), ZX_ERR_INVALID_ARGS);
}

TEST(ArchArm64, WriteVectorRegs) {
  std::vector<debug_ipc::Register> regs;

  std::vector<uint8_t> v0_value;
  v0_value.resize(16);
  v0_value.front() = 0x42;
  v0_value.back() = 0x12;
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_v0, v0_value);

  std::vector<uint8_t> v31_value = v0_value;
  v31_value.front()++;
  v31_value.back()++;
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_v31, v31_value);

  regs.emplace_back(debug_ipc::RegisterID::kARMv8_fpcr, std::vector<uint8_t>{5, 6, 7, 8});
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_fpsr, std::vector<uint8_t>{9, 0, 1, 2});

  zx_thread_state_vector_regs_t out = {};
  zx_status_t res = WriteVectorRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.v[0].low, 0x0000000000000042u);
  EXPECT_EQ(out.v[0].high, 0x1200000000000000u);
  EXPECT_EQ(out.v[31].low, 0x0000000000000043u);
  EXPECT_EQ(out.v[31].high, 0x1300000000000000u);

  EXPECT_EQ(out.fpcr, 0x08070605u);
  EXPECT_EQ(out.fpsr, 0x02010009u);
}

TEST(ArchArm64, WriteDebugRegs) {
  std::vector<debug_ipc::Register> regs;
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_dbgbcr0_el1, std::vector<uint8_t>{1, 2, 3, 4});
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_dbgbcr1_el1, std::vector<uint8_t>{2, 3, 4, 5});
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_dbgbcr15_el1, std::vector<uint8_t>{3, 4, 5, 6});

  regs.emplace_back(debug_ipc::RegisterID::kARMv8_dbgbvr0_el1,
                    std::vector<uint8_t>{4, 5, 6, 7, 8, 9, 0, 1});
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_dbgbvr1_el1,
                    std::vector<uint8_t>{5, 6, 7, 8, 9, 0, 1, 2});
  regs.emplace_back(debug_ipc::RegisterID::kARMv8_dbgbvr15_el1,
                    std::vector<uint8_t>{6, 7, 8, 9, 0, 1, 2, 3});

  // TODO(bug 40992) Add ARM64 hardware watchpoint registers here.

  zx_thread_state_debug_regs_t out = {};
  zx_status_t res = WriteDebugRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.hw_bps[0].dbgbcr, 0x04030201u);
  EXPECT_EQ(out.hw_bps[1].dbgbcr, 0x05040302u);
  EXPECT_EQ(out.hw_bps[15].dbgbcr, 0x06050403u);
  EXPECT_EQ(out.hw_bps[0].dbgbvr, 0x0100090807060504u);
  EXPECT_EQ(out.hw_bps[1].dbgbvr, 0x0201000908070605u);
  EXPECT_EQ(out.hw_bps[15].dbgbvr, 0x0302010009080706u);
}

TEST(ArchArm64, ReadTPIDR) {
  zx_thread_state_general_regs_t regs_in;
  regs_in.tpidr = 0xdeadbeeff00dbabe;

  std::vector<debug_ipc::Register> regs_out;
  arch::SaveGeneralRegs(regs_in, regs_out);

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

}  // namespace arch
}  // namespace debug_agent
