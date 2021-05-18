// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/unwinder/registers.h"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace unwinder {

Error Registers::Get(RegisterID reg_id, uint64_t& val) const {
  if (reg_id >= (arch_ == Arch::kX64 ? RegisterID::kX64_last : RegisterID::kArm64_last)) {
    return Error("invalid reg_id %hhu", reg_id);
  }
  auto it = regs_.find(reg_id);
  if (it == regs_.end()) {
    return Error("register %hhu is undefined", reg_id);
  }
  val = it->second;
  return Success();
}

Error Registers::Set(RegisterID reg_id, uint64_t val) {
  if (reg_id >= (arch_ == Arch::kX64 ? RegisterID::kX64_last : RegisterID::kArm64_last)) {
    return Error("invalid reg_id %hhu", reg_id);
  }
  regs_[reg_id] = val;
  return Success();
}

Error Registers::Unset(RegisterID reg_id) {
  if (reg_id >= (arch_ == Arch::kX64 ? RegisterID::kX64_last : RegisterID::kArm64_last)) {
    return Error("invalid reg_id %hhu", reg_id);
  }
  regs_.erase(reg_id);
  return Success();
}

Error Registers::GetSP(uint64_t& sp) const {
  return Get(arch_ == Arch::kX64 ? RegisterID::kX64_sp : RegisterID::kArm64_sp, sp);
}

Error Registers::SetSP(uint64_t sp) {
  return Set(arch_ == Arch::kX64 ? RegisterID::kX64_sp : RegisterID::kArm64_sp, sp);
}

Error Registers::GetPC(uint64_t& pc) const {
  return Get(arch_ == Arch::kX64 ? RegisterID::kX64_pc : RegisterID::kArm64_pc, pc);
}

Error Registers::SetPC(uint64_t pc) {
  return Set(arch_ == Arch::kX64 ? RegisterID::kX64_pc : RegisterID::kArm64_pc, pc);
}

Registers Registers::Clone() const {
  static RegisterID kX64Preserved[] = {
      RegisterID::kX64_rbx, RegisterID::kX64_rsp, RegisterID::kX64_rbp, RegisterID::kX64_r12,
      RegisterID::kX64_r13, RegisterID::kX64_r14, RegisterID::kX64_r15,
  };
  static RegisterID kARM64Preserved[] = {
      RegisterID::kArm64_x19, RegisterID::kArm64_x20, RegisterID::kArm64_x21,
      RegisterID::kArm64_x22, RegisterID::kArm64_x23, RegisterID::kArm64_x24,
      RegisterID::kArm64_x25, RegisterID::kArm64_x26, RegisterID::kArm64_x27,
      RegisterID::kArm64_x28, RegisterID::kArm64_x29, RegisterID::kArm64_x30,
      RegisterID::kArm64_x31,
  };

  RegisterID* preserved;
  size_t length;
  if (arch_ == Arch::kX64) {
    preserved = kX64Preserved;
    length = sizeof(kX64Preserved) / sizeof(RegisterID);
  } else {
    preserved = kARM64Preserved;
    length = sizeof(kARM64Preserved) / sizeof(RegisterID);
  }

  auto cloned = Registers(arch_);
  for (size_t i = 0; i < length; i++) {
    if (auto it = regs_.find(preserved[i]); it != regs_.end()) {
      cloned.regs_.emplace(it->first, it->second);
    }
  }
  return cloned;
}

std::string Registers::Describe() const {
  static const char* x64_names[] = {
      "rax", "rdx", "rcx", "rbx", "rsi", "rdi", "rbp", "rsp", "r8",
      "r9",  "r10", "r11", "r12", "r13", "r14", "r15", "rip",
  };
  static const char* arm64_names[] = {
      "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",  "x8",  "x9",  "x10",
      "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x19", "x20", "x21",
      "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "lr",  "sp",  "pc",
  };

  const char** names = arch_ == unwinder::Registers::Arch::kX64 ? x64_names : arm64_names;

  std::vector<RegisterID> keys;
  keys.reserve(regs_.size());
  for (const auto& [id, val] : regs_) {
    keys.push_back(id);
  }
  std::sort(keys.begin(), keys.end());

  std::stringstream ss;
  for (auto id : keys) {
    ss << names[static_cast<uint8_t>(id)] << "=0x" << std::hex << regs_.at(id) << " ";
  }

  std::string s = std::move(ss).str();
  // Remove the last space
  if (!s.empty()) {
    s.pop_back();
  }
  return s;
}

}  // namespace unwinder
