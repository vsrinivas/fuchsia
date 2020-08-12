// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/test_utils.h"
#include "src/developer/debug/ipc/register_test_support.h"
#include "src/developer/debug/shared/arch_x86.h"
#include "src/developer/debug/shared/logging/file_line_function.h"
#include "src/developer/debug/shared/zx_status.h"

namespace debug_agent {
namespace arch {

TEST(ArchX64, WriteGeneralRegs) {
  std::vector<debug_ipc::Register> regs;
  regs.push_back(CreateRegisterWithTestData(debug_ipc::RegisterID::kX64_rax, 8));
  regs.push_back(CreateRegisterWithTestData(debug_ipc::RegisterID::kX64_rbx, 8));
  regs.push_back(CreateRegisterWithTestData(debug_ipc::RegisterID::kX64_r14, 8));
  regs.push_back(CreateRegisterWithTestData(debug_ipc::RegisterID::kX64_rflags, 8));

  zx_thread_state_general_regs_t out = {};
  zx_status_t res = WriteGeneralRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.rax, 0x0102030405060708u);
  EXPECT_EQ(out.rbx, 0x0102030405060708u);
  EXPECT_EQ(out.rcx, 0u);
  EXPECT_EQ(out.rdx, 0u);
  EXPECT_EQ(out.rsi, 0u);
  EXPECT_EQ(out.rdi, 0u);
  EXPECT_EQ(out.rbp, 0u);
  EXPECT_EQ(out.rsp, 0u);
  EXPECT_EQ(out.r8, 0u);
  EXPECT_EQ(out.r9, 0u);
  EXPECT_EQ(out.r10, 0u);
  EXPECT_EQ(out.r11, 0u);
  EXPECT_EQ(out.r12, 0u);
  EXPECT_EQ(out.r13, 0u);
  EXPECT_EQ(out.r14, 0x0102030405060708u);
  EXPECT_EQ(out.r15, 0u);
  EXPECT_EQ(out.rip, 0u);
  EXPECT_EQ(out.rflags, 0x0102030405060708u);

  regs.clear();
  regs.emplace_back(debug_ipc::RegisterID::kX64_rax, static_cast<uint64_t>(0xaabb));
  regs.emplace_back(debug_ipc::RegisterID::kX64_rdx, static_cast<uint64_t>(0xdead));
  regs.emplace_back(debug_ipc::RegisterID::kX64_r10, static_cast<uint64_t>(0xbeef));

  res = WriteGeneralRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.rax, 0xaabbu);
  EXPECT_EQ(out.rbx, 0x0102030405060708u);
  EXPECT_EQ(out.rcx, 0u);
  EXPECT_EQ(out.rdx, 0xdeadu);
  EXPECT_EQ(out.rsi, 0u);
  EXPECT_EQ(out.rdi, 0u);
  EXPECT_EQ(out.rbp, 0u);
  EXPECT_EQ(out.rsp, 0u);
  EXPECT_EQ(out.r8, 0u);
  EXPECT_EQ(out.r9, 0u);
  EXPECT_EQ(out.r10, 0xbeefu);
  EXPECT_EQ(out.r11, 0u);
  EXPECT_EQ(out.r12, 0u);
  EXPECT_EQ(out.r13, 0u);
  EXPECT_EQ(out.r14, 0x0102030405060708u);
  EXPECT_EQ(out.r15, 0u);
  EXPECT_EQ(out.rip, 0u);
  EXPECT_EQ(out.rflags, 0x0102030405060708u);
}

TEST(ArchX64, InvalidWriteGeneralRegs) {
  zx_thread_state_general_regs_t out;
  std::vector<debug_ipc::Register> regs;

  // Invalid length.
  regs.push_back(CreateRegisterWithTestData(debug_ipc::RegisterID::kX64_rax, 4));
  EXPECT_EQ(WriteGeneralRegisters(regs, &out), ZX_ERR_INVALID_ARGS);

  // Invalid (non-canonical) register.
  regs.push_back(CreateRegisterWithTestData(debug_ipc::RegisterID::kX64_ymm2, 8));
  EXPECT_EQ(WriteGeneralRegisters(regs, &out), ZX_ERR_INVALID_ARGS);
}

TEST(ArchX64, WriteFPRegs) {
  std::vector<debug_ipc::Register> regs;
  regs.emplace_back(debug_ipc::RegisterID::kX64_fcw, std::vector<uint8_t>{1, 2});
  regs.emplace_back(debug_ipc::RegisterID::kX64_fsw, std::vector<uint8_t>{3, 4});
  regs.emplace_back(debug_ipc::RegisterID::kX64_ftw, std::vector<uint8_t>{6});
  regs.emplace_back(debug_ipc::RegisterID::kX64_fop, std::vector<uint8_t>{7, 8});
  regs.emplace_back(debug_ipc::RegisterID::kX64_fip, std::vector<uint8_t>{9, 0, 0, 0, 10, 0, 0, 0});
  regs.emplace_back(debug_ipc::RegisterID::kX64_fdp,
                    std::vector<uint8_t>{11, 0, 0, 0, 12, 0, 0, 0});

  zx_thread_state_fp_regs_t out = {};
  zx_status_t res = WriteFloatingPointRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.fcw, 0x0201u);
  EXPECT_EQ(out.fsw, 0x0403u);
  EXPECT_EQ(out.ftw, 0x06u);
  EXPECT_EQ(out.fop, 0x0807u);
  EXPECT_EQ(out.fip, 0x0000000a00000009u);
  EXPECT_EQ(out.fdp, 0x0000000c0000000bu);
}

TEST(ArchX64, WriteVectorRegs) {
  std::vector<debug_ipc::Register> regs;

  std::vector<uint8_t> zmm0_value;
  zmm0_value.resize(64);
  zmm0_value.front() = 0x42;
  zmm0_value.back() = 0x12;
  regs.emplace_back(debug_ipc::RegisterID::kX64_zmm0, zmm0_value);

  std::vector<uint8_t> zmm31_value = zmm0_value;
  zmm31_value.front()++;
  zmm31_value.back()++;
  regs.emplace_back(debug_ipc::RegisterID::kX64_zmm31, zmm31_value);

  regs.emplace_back(debug_ipc::RegisterID::kX64_mxcsr, std::vector<uint8_t>{5, 6, 7, 8});

  zx_thread_state_vector_regs_t out = {};
  zx_status_t res = WriteVectorRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.zmm[0].v[0], 0x0000000000000042u);
  EXPECT_EQ(out.zmm[0].v[1], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[0].v[2], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[0].v[3], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[0].v[4], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[0].v[5], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[0].v[6], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[0].v[7], 0x1200000000000000u);

  EXPECT_EQ(out.zmm[31].v[0], 0x0000000000000043u);
  EXPECT_EQ(out.zmm[31].v[1], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[31].v[2], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[31].v[3], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[31].v[4], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[31].v[5], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[31].v[6], 0x0000000000000000u);
  EXPECT_EQ(out.zmm[31].v[7], 0x1300000000000000u);

  EXPECT_EQ(out.mxcsr, 0x08070605u);
}

TEST(ArchX64, WriteDebugRegs) {
  std::vector<debug_ipc::Register> regs;
  regs.emplace_back(debug_ipc::RegisterID::kX64_dr0, std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8});
  regs.emplace_back(debug_ipc::RegisterID::kX64_dr1, std::vector<uint8_t>{2, 3, 4, 5, 6, 7, 8, 9});
  regs.emplace_back(debug_ipc::RegisterID::kX64_dr2, std::vector<uint8_t>{3, 4, 5, 6, 7, 8, 9, 0});
  regs.emplace_back(debug_ipc::RegisterID::kX64_dr3, std::vector<uint8_t>{4, 5, 6, 7, 8, 9, 0, 1});
  regs.emplace_back(debug_ipc::RegisterID::kX64_dr6, std::vector<uint8_t>{5, 6, 7, 8, 9, 0, 1, 2});
  regs.emplace_back(debug_ipc::RegisterID::kX64_dr7, std::vector<uint8_t>{6, 7, 8, 9, 0, 1, 2, 3});

  zx_thread_state_debug_regs_t out = {};
  zx_status_t res = WriteDebugRegisters(regs, &out);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(res);

  EXPECT_EQ(out.dr[0], 0x0807060504030201u);
  EXPECT_EQ(out.dr[1], 0x0908070605040302u);
  EXPECT_EQ(out.dr[2], 0x0009080706050403u);
  EXPECT_EQ(out.dr[3], 0x0100090807060504u);
  EXPECT_EQ(out.dr6, 0x0201000908070605u);
  EXPECT_EQ(out.dr7, 0x0302010009080706u);
}

TEST(ArchX64, ReadSegmentRegs) {
  zx_thread_state_general_regs_t regs_in;
  regs_in.fs_base = 0xdeadbeeff00dbabe;
  regs_in.gs_base = 0xabadd00dbeadfeed;
  std::vector<debug_ipc::Register> regs_out;
  SaveGeneralRegs(regs_in, regs_out);

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

}  // namespace arch
}  // namespace debug_agent
