// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/third_party/libunwindstack/fuchsia/RegsFuchsia.h"

#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>

#include "unwindstack/Elf.h"
#include "unwindstack/MachineX86_64.h"
#include "unwindstack/Memory.h"

namespace unwindstack {

namespace {

constexpr uint16_t kUnwindStackRegCount = X86_64_REG_LAST;

}  // namespace

RegsFuchsia::RegsFuchsia()
    : RegsImpl<uint64_t>(kUnwindStackRegCount,
                         Location(LOCATION_SP_OFFSET, -8)) {}

RegsFuchsia::~RegsFuchsia() = default;

ArchEnum RegsFuchsia::Arch() { return ARCH_X86_64; }

zx_status_t RegsFuchsia::Read(zx_handle_t thread) {
  zx_thread_state_general_regs thread_regs;
  zx_status_t status = zx_thread_read_state(
      thread, ZX_THREAD_STATE_GENERAL_REGS, &thread_regs, sizeof(thread_regs));
  if (status != ZX_OK)
    return status;

  regs_.resize(kUnwindStackRegCount);

  regs_[X86_64_REG_RAX] = thread_regs.rax;
  regs_[X86_64_REG_RBX] = thread_regs.rbx;
  regs_[X86_64_REG_RCX] = thread_regs.rcx;
  regs_[X86_64_REG_RDX] = thread_regs.rdx;
  regs_[X86_64_REG_RSI] = thread_regs.rsi;
  regs_[X86_64_REG_RDI] = thread_regs.rdi;
  regs_[X86_64_REG_RBP] = thread_regs.rbp;
  regs_[X86_64_REG_RSP] = thread_regs.rsp;
  regs_[X86_64_REG_R8] = thread_regs.r8;
  regs_[X86_64_REG_R9] = thread_regs.r9;
  regs_[X86_64_REG_R10] = thread_regs.r10;
  regs_[X86_64_REG_R11] = thread_regs.r11;
  regs_[X86_64_REG_R12] = thread_regs.r12;
  regs_[X86_64_REG_R13] = thread_regs.r13;
  regs_[X86_64_REG_R14] = thread_regs.r14;
  regs_[X86_64_REG_R15] = thread_regs.r15;
  regs_[X86_64_REG_RIP] = thread_regs.rip;

  return ZX_OK;
}

uint64_t RegsFuchsia::GetPcAdjustment(uint64_t rel_pc, Elf* elf) {
  // TODO(brettw) I have no idea what this means. If we return 1 here like
  // the Android one does sometimes, return addresses are off-by-one.
  return 0;
}

bool RegsFuchsia::SetPcFromReturnAddress(Memory* process_memory) {
  // Attempt to get the return address from the top of the stack.
  uint64_t new_pc;
  if (!process_memory->ReadFully(regs_[X86_64_REG_SP], &new_pc,
                                 sizeof(new_pc)) ||
      new_pc == regs_[X86_64_REG_PC]) {
    return false;
  }

  regs_[X86_64_REG_PC] = new_pc;
  return true;
}

bool RegsFuchsia::StepIfSignalHandler(uint64_t rel_pc, Elf* elf,
                                      Memory* process_memory) {
  // TODO(brettw) Figure out if we need to implement this.
  return false;
}

void RegsFuchsia::IterateRegisters(
    std::function<void(const char*, uint64_t)> fn) {
  fn("rax", regs_[X86_64_REG_RAX]);
  fn("rbx", regs_[X86_64_REG_RBX]);
  fn("rcx", regs_[X86_64_REG_RCX]);
  fn("rdx", regs_[X86_64_REG_RDX]);
  fn("r8", regs_[X86_64_REG_R8]);
  fn("r9", regs_[X86_64_REG_R9]);
  fn("r10", regs_[X86_64_REG_R10]);
  fn("r11", regs_[X86_64_REG_R11]);
  fn("r12", regs_[X86_64_REG_R12]);
  fn("r13", regs_[X86_64_REG_R13]);
  fn("r14", regs_[X86_64_REG_R14]);
  fn("r15", regs_[X86_64_REG_R15]);
  fn("rdi", regs_[X86_64_REG_RDI]);
  fn("rsi", regs_[X86_64_REG_RSI]);
  fn("rbp", regs_[X86_64_REG_RBP]);
  fn("rsp", regs_[X86_64_REG_RSP]);
  fn("rip", regs_[X86_64_REG_RIP]);
}

uint64_t RegsFuchsia::pc() { return regs_[X86_64_REG_PC]; }

uint64_t RegsFuchsia::sp() { return regs_[X86_64_REG_SP]; }

void RegsFuchsia::set_pc(uint64_t pc) { regs_[X86_64_REG_PC] = pc; }

void RegsFuchsia::set_sp(uint64_t sp) { regs_[X86_64_REG_SP] = sp; }

Regs* RegsFuchsia::Clone() { return new RegsFuchsia(*this); }

}  // namespace unwindstack
