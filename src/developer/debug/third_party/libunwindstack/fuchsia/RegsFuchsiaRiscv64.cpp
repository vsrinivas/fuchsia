// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/third_party/libunwindstack/fuchsia/RegsFuchsia.h"

#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>

#include "unwindstack/Elf.h"
#include "unwindstack/MachineArm64.h"
#include "unwindstack/MachineX86_64.h"
#include "unwindstack/Memory.h"

namespace unwindstack {

namespace {

constexpr uint16_t kUnwindStackRegCount = 0;

}  // namespace

RegsFuchsia::RegsFuchsia()
    : RegsImpl<uint64_t>(kUnwindStackRegCount,
                         Location(LOCATION_SP_OFFSET, -8)) {}

RegsFuchsia::~RegsFuchsia() = default;

ArchEnum RegsFuchsia::Arch() { return ARCH_RISCV64; }

void RegsFuchsia::Set(const zx_thread_state_general_regs& input) {
}

zx_status_t RegsFuchsia::Read(zx_handle_t thread) {
  return ZX_OK;
}

uint64_t RegsFuchsia::GetPcAdjustment(uint64_t rel_pc, Elf* elf) {
  return 0;
}

bool RegsFuchsia::SetPcFromReturnAddress(Memory* process_memory) {
  return false;
}

bool RegsFuchsia::StepIfSignalHandler(uint64_t rel_pc, Elf* elf,
                                      Memory* process_memory) {
  return false;
}

void RegsFuchsia::IterateRegisters(
    std::function<void(const char*, uint64_t)> fn) {
}

uint64_t RegsFuchsia::pc() { return 0; }

uint64_t RegsFuchsia::sp() { return 0; }

void RegsFuchsia::set_pc(uint64_t pc) { }

void RegsFuchsia::set_sp(uint64_t sp) { }

Regs* RegsFuchsia::Clone() { return new RegsFuchsia(*this); }

}  // namespace unwindstack
