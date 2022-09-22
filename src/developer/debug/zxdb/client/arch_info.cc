// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/arch_info.h"

#include <lib/syslog/cpp/macros.h>

#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "src/developer/debug/zxdb/expr/abi_arm64.h"
#include "src/developer/debug/zxdb/expr/abi_x64.h"

namespace zxdb {

namespace {

std::unique_ptr<llvm::InitLLVM> init_llvm;

}  // namespace

ArchInfo::ArchInfo() {
  if (!init_llvm) {
    int argc = 0;
    const char* arg = nullptr;
    const char** argv = &arg;
    init_llvm = std::make_unique<llvm::InitLLVM>(argc, argv);

    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllDisassemblers();
  }
}

ArchInfo::~ArchInfo() = default;

Err ArchInfo::Init(debug::Arch arch, uint64_t page_size) {
  arch_ = arch;
  page_size_ = page_size;

  switch (arch) {
    case debug::Arch::kUnknown:
      // This is used for some tests and default values. LLVM will not be initialized.
      return Err();

    case debug::Arch::kX64:
      abi_ = std::make_shared<AbiX64>();
      is_fixed_instr_ = false;
      max_instr_len_ = 15;
      instr_align_ = 1;
      triple_name_ = "x86_64";
      processor_name_ = "x86-64";
      break;
    case debug::Arch::kArm64:
      abi_ = std::make_shared<AbiArm64>();
      is_fixed_instr_ = true;
      max_instr_len_ = 4;
      instr_align_ = 4;
      triple_name_ = "aarch64";
      processor_name_ = "generic";
      break;
    default:
      FX_NOTREACHED();
      break;
  }

  triple_ = std::make_unique<llvm::Triple>(triple_name_);

  std::string err_msg;
  target_ = llvm::TargetRegistry::lookupTarget(triple_name_, err_msg);
  if (!target_)
    return Err("Error initializing LLVM: " + err_msg);

  instr_info_.reset(target_->createMCInstrInfo());
  register_info_.reset(target_->createMCRegInfo(triple_name_));
  subtarget_info_.reset(target_->createMCSubtargetInfo(triple_name_, processor_name_, ""));
  asm_info_.reset(target_->createMCAsmInfo(*register_info_, triple_name_, {}));

  if (!instr_info_ || !register_info_ || !subtarget_info_ || !asm_info_)
    return Err("Error initializing LLVM.");

  return Err();
}

}  // namespace zxdb
