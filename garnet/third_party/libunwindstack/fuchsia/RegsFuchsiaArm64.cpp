// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/third_party/libunwindstack/fuchsia/RegsFuchsia.h"

#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>

#include "unwindstack/Elf.h"
#include "unwindstack/MachineArm64.h"
#include "unwindstack/MachineX86_64.h"
#include "unwindstack/Memory.h"

namespace unwindstack {

namespace {

constexpr uint16_t kUnwindStackRegCount = ARM64_REG_LAST;

}  // namespace

RegsFuchsia::RegsFuchsia()
    : RegsImpl<uint64_t>(kUnwindStackRegCount,
                         Location(LOCATION_SP_OFFSET, -8)) {}

RegsFuchsia::~RegsFuchsia() = default;

ArchEnum RegsFuchsia::Arch() { return ARCH_ARM64; }

void RegsFuchsia::Set(const zx_thread_state_general_regs& input) {
  regs_.resize(kUnwindStackRegCount);

  regs_[ARM64_REG_R0] = input.r[0];
  regs_[ARM64_REG_R1] = input.r[1];
  regs_[ARM64_REG_R2] = input.r[2];
  regs_[ARM64_REG_R3] = input.r[3];
  regs_[ARM64_REG_R4] = input.r[4];
  regs_[ARM64_REG_R5] = input.r[5];
  regs_[ARM64_REG_R6] = input.r[6];
  regs_[ARM64_REG_R7] = input.r[7];
  regs_[ARM64_REG_R8] = input.r[8];
  regs_[ARM64_REG_R9] = input.r[9];
  regs_[ARM64_REG_R10] = input.r[10];
  regs_[ARM64_REG_R11] = input.r[11];
  regs_[ARM64_REG_R12] = input.r[12];
  regs_[ARM64_REG_R13] = input.r[13];
  regs_[ARM64_REG_R14] = input.r[14];
  regs_[ARM64_REG_R15] = input.r[15];
  regs_[ARM64_REG_R16] = input.r[16];
  regs_[ARM64_REG_R17] = input.r[17];
  regs_[ARM64_REG_R18] = input.r[18];
  regs_[ARM64_REG_R19] = input.r[19];
  regs_[ARM64_REG_R20] = input.r[20];
  regs_[ARM64_REG_R21] = input.r[21];
  regs_[ARM64_REG_R22] = input.r[22];
  regs_[ARM64_REG_R23] = input.r[23];
  regs_[ARM64_REG_R24] = input.r[24];
  regs_[ARM64_REG_R25] = input.r[25];
  regs_[ARM64_REG_R26] = input.r[26];
  regs_[ARM64_REG_R27] = input.r[27];
  regs_[ARM64_REG_R28] = input.r[28];
  regs_[ARM64_REG_R29] = input.r[29];
  regs_[ARM64_REG_LR] = input.lr;
  regs_[ARM64_REG_SP] = input.sp;
  regs_[ARM64_REG_PC] = input.pc;
}

zx_status_t RegsFuchsia::Read(zx_handle_t thread) {
  zx_thread_state_general_regs thread_regs;
  zx_status_t status = zx_thread_read_state(
      thread, ZX_THREAD_STATE_GENERAL_REGS, &thread_regs, sizeof(thread_regs));
  if (status != ZX_OK)
    return status;

  Set(thread_regs);
  return ZX_OK;
}

uint64_t RegsFuchsia::GetPcAdjustment(uint64_t rel_pc, Elf* elf) {
  // Android uses this to "fix" the IP (source of the call instead of the
  // return address). But we expect no adjustment.
  return 0;
}

bool RegsFuchsia::SetPcFromReturnAddress(Memory* process_memory) {
  uint64_t lr = regs_[ARM64_REG_LR];
  if (regs_[ARM64_REG_PC] == lr) {
    return false;
  }

  regs_[ARM64_REG_PC] = lr;
  return true;
}

bool RegsFuchsia::StepIfSignalHandler(uint64_t rel_pc, Elf* elf,
                                      Memory* process_memory) {
  // TODO(brettw) Figure out if we need to implement this.
  return false;
}

void RegsFuchsia::IterateRegisters(
    std::function<void(const char*, uint64_t)> fn) {
  fn("x0", regs_[ARM64_REG_R0]);
  fn("x1", regs_[ARM64_REG_R1]);
  fn("x2", regs_[ARM64_REG_R2]);
  fn("x3", regs_[ARM64_REG_R3]);
  fn("x4", regs_[ARM64_REG_R4]);
  fn("x5", regs_[ARM64_REG_R5]);
  fn("x6", regs_[ARM64_REG_R6]);
  fn("x7", regs_[ARM64_REG_R7]);
  fn("x8", regs_[ARM64_REG_R8]);
  fn("x9", regs_[ARM64_REG_R9]);
  fn("x10", regs_[ARM64_REG_R10]);
  fn("x11", regs_[ARM64_REG_R11]);
  fn("x12", regs_[ARM64_REG_R12]);
  fn("x13", regs_[ARM64_REG_R13]);
  fn("x14", regs_[ARM64_REG_R14]);
  fn("x15", regs_[ARM64_REG_R15]);
  fn("x16", regs_[ARM64_REG_R16]);
  fn("x17", regs_[ARM64_REG_R17]);
  fn("x18", regs_[ARM64_REG_R18]);
  fn("x19", regs_[ARM64_REG_R19]);
  fn("x20", regs_[ARM64_REG_R20]);
  fn("x21", regs_[ARM64_REG_R21]);
  fn("x22", regs_[ARM64_REG_R22]);
  fn("x23", regs_[ARM64_REG_R23]);
  fn("x24", regs_[ARM64_REG_R24]);
  fn("x25", regs_[ARM64_REG_R25]);
  fn("x26", regs_[ARM64_REG_R26]);
  fn("x27", regs_[ARM64_REG_R27]);
  fn("x28", regs_[ARM64_REG_R28]);
  fn("x29", regs_[ARM64_REG_R29]);
  fn("sp", regs_[ARM64_REG_SP]);
  fn("lr", regs_[ARM64_REG_LR]);
  fn("pc", regs_[ARM64_REG_PC]);
}

uint64_t RegsFuchsia::pc() { return regs_[ARM64_REG_PC]; }

uint64_t RegsFuchsia::sp() { return regs_[ARM64_REG_SP]; }

void RegsFuchsia::set_pc(uint64_t pc) { regs_[ARM64_REG_PC] = pc; }

void RegsFuchsia::set_sp(uint64_t sp) { regs_[ARM64_REG_SP] = sp; }

Regs* RegsFuchsia::Clone() { return new RegsFuchsia(*this); }

}  // namespace unwindstack
