// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_UNWINDER_REGISTERS_H_
#define SRC_DEVELOPER_DEBUG_UNWINDER_REGISTERS_H_

#include <cstdint>
#include <map>

#include "src/developer/debug/unwinder/error.h"

namespace unwinder {

// The DWARF ID for each register.
enum class RegisterID : uint8_t {
  // x86_64. Please note the order is not RAX, RBX, RCX, RDX as in zx_thread_state_general_regs.
  kX64_rax = 0,
  kX64_rdx = 1,
  kX64_rcx = 2,
  kX64_rbx = 3,
  kX64_rsi = 4,
  kX64_rdi = 5,
  kX64_rbp = 6,
  kX64_rsp = 7,
  kX64_r8 = 8,
  kX64_r9 = 9,
  kX64_r10 = 10,
  kX64_r11 = 11,
  kX64_r12 = 12,
  kX64_r13 = 13,
  kX64_r14 = 14,
  kX64_r15 = 15,
  kX64_rip = 16,
  kX64_last,

  kX64_sp = kX64_rsp,
  kX64_pc = kX64_rip,

  // arm64
  kArm64_x0 = 0,
  kArm64_x1 = 1,
  kArm64_x2 = 2,
  kArm64_x3 = 3,
  kArm64_x4 = 4,
  kArm64_x5 = 5,
  kArm64_x6 = 6,
  kArm64_x7 = 7,
  kArm64_x8 = 8,
  kArm64_x9 = 9,
  kArm64_x10 = 10,
  kArm64_x11 = 11,
  kArm64_x12 = 12,
  kArm64_x13 = 13,
  kArm64_x14 = 14,
  kArm64_x15 = 15,
  kArm64_x16 = 16,
  kArm64_x17 = 17,
  kArm64_x18 = 18,
  kArm64_x19 = 19,
  kArm64_x20 = 20,
  kArm64_x21 = 21,
  kArm64_x22 = 22,
  kArm64_x23 = 23,
  kArm64_x24 = 24,
  kArm64_x25 = 25,
  kArm64_x26 = 26,
  kArm64_x27 = 27,
  kArm64_x28 = 28,
  kArm64_x29 = 29,
  kArm64_x30 = 30,
  kArm64_x31 = 31,
  kArm64_pc = 32,
  kArm64_last,

  kArm64_sp = kArm64_x31,
  kArm64_lr = kArm64_x30,

  kInvalid = static_cast<uint8_t>(-1),
};

class Registers {
 public:
  enum class Arch {
    kX64,
    kArm64,
  };

  explicit Registers(Arch arch) : arch_(arch) {}

  Arch arch() const { return arch_; }
  auto begin() const { return regs_.begin(); }
  auto end() const { return regs_.end(); }

  Error Get(RegisterID reg_id, uint64_t& val) const;
  Error Set(RegisterID reg_id, uint64_t val);
  Error Unset(RegisterID reg_id);

  Error GetSP(uint64_t& sp) const;
  Error SetSP(uint64_t sp);
  Error GetPC(uint64_t& pc) const;
  Error SetPC(uint64_t pc);

  // Create new registers by keeping values in preserved registers during a call (callee-saved).
  //
  // This should be unnecessary if CFI could encode all registers with either DW_CFA_undefined or
  // DW_CFA_same_value properly.
  Registers Clone() const;

  // Return a string describing the value of all registers. Should be useful in debugging.
  std::string Describe() const;

 private:
  Arch arch_;
  std::map<RegisterID, uint64_t> regs_;
};

}  // namespace unwinder

#endif  // SRC_DEVELOPER_DEBUG_UNWINDER_REGISTERS_H_
