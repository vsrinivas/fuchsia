// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/public/lib/fxl/macros.h"

namespace llvm {
class InitLLVM;
class MCInstrInfo;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCAsmInfo;
class Target;
class Triple;
}  // namespace llvm

namespace zxdb {

class ArchInfo {
 public:
  ArchInfo();
  ~ArchInfo();

  Err Init(debug_ipc::Arch arch);

  // Returns true of the instruction length is fixed.
  bool is_fixed_instr() const { return is_fixed_instr_; }

  // Minimum instruction alignment. Prefer instead of
  // llvm::AsmInfo::MinInstAlignment which isn't correct for ARM (reports 1).
  size_t instr_align() const { return instr_align_; }

  // Longest possible instruction in bytes. Prefer instead of
  // llvm::AsmInfo::MaxInstLength which isn't correct for x86 (reports 1).
  size_t max_instr_len() const { return max_instr_len_; }

  // In LLVM a configuration name is called a "triple" even though it contains
  // more than 3 fields.
  const std::string& triple_name() const { return triple_name_; }
  const llvm::Triple* triple() const { return triple_.get(); }

  const std::string& processor_name() const { return processor_name_; }

  const llvm::Target* target() const { return target_; }
  const llvm::MCInstrInfo* instr_info() const { return instr_info_.get(); }
  const llvm::MCRegisterInfo* register_info() const {
    return register_info_.get();
  }
  const llvm::MCSubtargetInfo* subtarget_info() const {
    return subtarget_info_.get();
  }
  const llvm::MCAsmInfo* asm_info() const { return asm_info_.get(); }

 private:
  bool is_fixed_instr_ = false;
  size_t instr_align_ = 1;
  size_t max_instr_len_ = 1;

  std::string triple_name_;
  std::string processor_name_;

  std::unique_ptr<llvm::InitLLVM> init_;
  std::unique_ptr<llvm::Triple> triple_;

  const llvm::Target* target_ = nullptr;  // Non-owning.
  std::unique_ptr<llvm::MCInstrInfo> instr_info_;
  std::unique_ptr<llvm::MCRegisterInfo> register_info_;
  std::unique_ptr<llvm::MCSubtargetInfo> subtarget_info_;
  std::unique_ptr<llvm::MCAsmInfo> asm_info_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ArchInfo);
};

}  // namespace zxdb
