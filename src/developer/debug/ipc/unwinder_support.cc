// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/unwinder_support.h"

#include "src/developer/debug/unwinder/registers.h"

namespace debug_ipc {

namespace {

// Convert from unwinder::RegisterID to debug_ipc::RegisterID.
debug_ipc::RegisterID ConvertRegisterID(unwinder::Registers::Arch arch,
                                        unwinder::RegisterID reg_id) {
  using Map = std::map<unwinder::RegisterID, debug_ipc::RegisterID>;

  static Map x64_map = {
      {unwinder::RegisterID::kX64_rax, debug_ipc::RegisterID::kX64_rax},
      {unwinder::RegisterID::kX64_rdx, debug_ipc::RegisterID::kX64_rdx},
      {unwinder::RegisterID::kX64_rcx, debug_ipc::RegisterID::kX64_rcx},
      {unwinder::RegisterID::kX64_rbx, debug_ipc::RegisterID::kX64_rbx},
      {unwinder::RegisterID::kX64_rsi, debug_ipc::RegisterID::kX64_rsi},
      {unwinder::RegisterID::kX64_rdi, debug_ipc::RegisterID::kX64_rdi},
      {unwinder::RegisterID::kX64_rbp, debug_ipc::RegisterID::kX64_rbp},
      {unwinder::RegisterID::kX64_rsp, debug_ipc::RegisterID::kX64_rsp},
      {unwinder::RegisterID::kX64_r8, debug_ipc::RegisterID::kX64_r8},
      {unwinder::RegisterID::kX64_r9, debug_ipc::RegisterID::kX64_r9},
      {unwinder::RegisterID::kX64_r10, debug_ipc::RegisterID::kX64_r10},
      {unwinder::RegisterID::kX64_r11, debug_ipc::RegisterID::kX64_r11},
      {unwinder::RegisterID::kX64_r12, debug_ipc::RegisterID::kX64_r12},
      {unwinder::RegisterID::kX64_r13, debug_ipc::RegisterID::kX64_r13},
      {unwinder::RegisterID::kX64_r14, debug_ipc::RegisterID::kX64_r14},
      {unwinder::RegisterID::kX64_r15, debug_ipc::RegisterID::kX64_r15},
      {unwinder::RegisterID::kX64_rip, debug_ipc::RegisterID::kX64_rip},
  };
  static Map arm64_map = {
      {unwinder::RegisterID::kArm64_x0, debug_ipc::RegisterID::kARMv8_x0},
      {unwinder::RegisterID::kArm64_x1, debug_ipc::RegisterID::kARMv8_x1},
      {unwinder::RegisterID::kArm64_x2, debug_ipc::RegisterID::kARMv8_x2},
      {unwinder::RegisterID::kArm64_x3, debug_ipc::RegisterID::kARMv8_x3},
      {unwinder::RegisterID::kArm64_x4, debug_ipc::RegisterID::kARMv8_x4},
      {unwinder::RegisterID::kArm64_x5, debug_ipc::RegisterID::kARMv8_x5},
      {unwinder::RegisterID::kArm64_x6, debug_ipc::RegisterID::kARMv8_x6},
      {unwinder::RegisterID::kArm64_x7, debug_ipc::RegisterID::kARMv8_x7},
      {unwinder::RegisterID::kArm64_x8, debug_ipc::RegisterID::kARMv8_x8},
      {unwinder::RegisterID::kArm64_x9, debug_ipc::RegisterID::kARMv8_x9},
      {unwinder::RegisterID::kArm64_x10, debug_ipc::RegisterID::kARMv8_x10},
      {unwinder::RegisterID::kArm64_x11, debug_ipc::RegisterID::kARMv8_x11},
      {unwinder::RegisterID::kArm64_x12, debug_ipc::RegisterID::kARMv8_x12},
      {unwinder::RegisterID::kArm64_x13, debug_ipc::RegisterID::kARMv8_x13},
      {unwinder::RegisterID::kArm64_x14, debug_ipc::RegisterID::kARMv8_x14},
      {unwinder::RegisterID::kArm64_x15, debug_ipc::RegisterID::kARMv8_x15},
      {unwinder::RegisterID::kArm64_x16, debug_ipc::RegisterID::kARMv8_x16},
      {unwinder::RegisterID::kArm64_x17, debug_ipc::RegisterID::kARMv8_x17},
      {unwinder::RegisterID::kArm64_x18, debug_ipc::RegisterID::kARMv8_x18},
      {unwinder::RegisterID::kArm64_x19, debug_ipc::RegisterID::kARMv8_x19},
      {unwinder::RegisterID::kArm64_x20, debug_ipc::RegisterID::kARMv8_x20},
      {unwinder::RegisterID::kArm64_x21, debug_ipc::RegisterID::kARMv8_x21},
      {unwinder::RegisterID::kArm64_x22, debug_ipc::RegisterID::kARMv8_x22},
      {unwinder::RegisterID::kArm64_x23, debug_ipc::RegisterID::kARMv8_x23},
      {unwinder::RegisterID::kArm64_x24, debug_ipc::RegisterID::kARMv8_x24},
      {unwinder::RegisterID::kArm64_x25, debug_ipc::RegisterID::kARMv8_x25},
      {unwinder::RegisterID::kArm64_x26, debug_ipc::RegisterID::kARMv8_x26},
      {unwinder::RegisterID::kArm64_x27, debug_ipc::RegisterID::kARMv8_x27},
      {unwinder::RegisterID::kArm64_x28, debug_ipc::RegisterID::kARMv8_x28},
      {unwinder::RegisterID::kArm64_x29, debug_ipc::RegisterID::kARMv8_x29},
      {unwinder::RegisterID::kArm64_x30, debug_ipc::RegisterID::kARMv8_lr},
      {unwinder::RegisterID::kArm64_x31, debug_ipc::RegisterID::kARMv8_sp},
      {unwinder::RegisterID::kArm64_pc, debug_ipc::RegisterID::kARMv8_pc},
  };

  Map* map = nullptr;

  switch (arch) {
    case unwinder::Registers::Arch::kX64:
      map = &x64_map;
      break;
    case unwinder::Registers::Arch::kArm64:
      map = &arm64_map;
      break;
  }

  auto found = map->find(reg_id);
  FX_CHECK(found != map->end());
  return found->second;
}

}  // namespace

std::vector<debug_ipc::StackFrame> ConvertFrames(const std::vector<unwinder::Frame>& frames) {
  std::vector<debug_ipc::StackFrame> res;

  for (const unwinder::Frame& frame : frames) {
    std::vector<debug_ipc::Register> frame_regs;
    uint64_t ip = 0;
    uint64_t sp = 0;
    frame.regs.GetSP(sp);
    frame.regs.GetPC(ip);
    if (!res.empty()) {
      res.back().cfa = sp;
    }
    frame_regs.reserve(frame.regs.size());
    for (auto& [reg_id, val] : frame.regs) {
      frame_regs.emplace_back(ConvertRegisterID(frame.regs.arch(), reg_id), val);
    }
    res.emplace_back(ip, sp, 0, frame_regs);
  }

  return res;
}

}  // namespace debug_ipc
