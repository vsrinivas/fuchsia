// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/software_breakpoint.h"

#include <inttypes.h>

#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/debug_agent/process_memory_accessor.h"
#include "src/lib/fxl/logging.h"

namespace debug_agent {

SoftwareBreakpoint::SoftwareBreakpoint(Breakpoint* breakpoint, DebuggedProcess* process,
                                       ProcessMemoryAccessor* memory_accessor, uint64_t address)
    : ProcessBreakpoint(breakpoint, process, address), memory_accessor_(memory_accessor) {}

SoftwareBreakpoint::~SoftwareBreakpoint() { Uninstall(); }

zx_status_t SoftwareBreakpoint::Update() {
  // Software breakpoints remain installed as long as even one remains active, regardless of which
  // threads are targeted.
  int sw_bp_count = 0;
  for (Breakpoint* bp : breakpoints()) {
    if (bp->type() == debug_ipc::BreakpointType::kSoftware)
      sw_bp_count++;
  }

  if (sw_bp_count == 0 && Installed()) {
    Uninstall();
  } else if (sw_bp_count > 0 && !Installed()) {
    return Install();
  }

  return ZX_OK;
}

zx_status_t SoftwareBreakpoint::Install() {
  FXL_DCHECK(!installed_);

  // Read previous instruction contents.
  size_t actual = 0;
  zx_status_t status = memory_accessor_->ReadProcessMemory(
      address(), &previous_data_, sizeof(arch::BreakInstructionType), &actual);
  if (status != ZX_OK)
    return status;
  if (actual != sizeof(arch::BreakInstructionType))
    return ZX_ERR_UNAVAILABLE;

  // Replace with breakpoint instruction.
  status = memory_accessor_->WriteProcessMemory(address(), &arch::kBreakInstruction,
                                                sizeof(arch::BreakInstructionType), &actual);
  if (status != ZX_OK)
    return status;
  if (actual != sizeof(arch::BreakInstructionType))
    return ZX_ERR_UNAVAILABLE;

  installed_ = true;
  return ZX_OK;
}

void SoftwareBreakpoint::Uninstall() {
  if (!installed_)
    return;  // Not installed.

  // If the breakpoint was previously installed it means the memory address was valid and writable,
  // so we generally expect to be able to do the same write to uninstall it. But it could have been
  // unmapped during execution or even remapped with something else. So verify that it's still a
  // breakpoint instruction before doing any writes.
  arch::BreakInstructionType current_contents = 0;
  size_t actual = 0;
  zx_status_t status = memory_accessor_->ReadProcessMemory(
      address(), &current_contents, sizeof(arch::BreakInstructionType), &actual);
  if (status != ZX_OK || actual != sizeof(arch::BreakInstructionType))
    return;  // Probably unmapped, safe to ignore.

  if (current_contents != arch::kBreakInstruction) {
    FXL_LOG(WARNING) << "Debug break instruction unexpectedly replaced at 0x" << std::hex
                     << address();
    return;  // Replaced with something else, ignore.
  }

  status = memory_accessor_->WriteProcessMemory(address(), &previous_data_,
                                                sizeof(arch::BreakInstructionType), &actual);
  if (status != ZX_OK || actual != sizeof(arch::BreakInstructionType)) {
    FXL_LOG(WARNING) << "Unable to remove breakpoint at 0x" << std::hex << address() << ": "
                     << zx_status_get_string(status);
  }

  installed_ = false;
}

void SoftwareBreakpoint::FixupMemoryBlock(debug_ipc::MemoryBlock* block) {
  if (block->data.empty())
    return;  // Nothing to do.
  FXL_DCHECK(static_cast<size_t>(block->size) == block->data.size());

  size_t src_size = sizeof(arch::BreakInstructionType);
  const uint8_t* src = reinterpret_cast<uint8_t*>(&previous_data_);

  // Simple implementation to prevent boundary errors (ARM instructions are 32-bits and could be
  // hanging partially off either end of the requested buffer).
  for (size_t i = 0; i < src_size; i++) {
    uint64_t dest_address = address() + i;
    if (dest_address >= block->address && dest_address < block->address + block->size)
      block->data[dest_address - block->address] = src[i];
  }
}

}  // namespace debug_agent
