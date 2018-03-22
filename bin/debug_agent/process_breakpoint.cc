// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/process_breakpoint.h"

#include <inttypes.h>

#include "garnet/bin/debug_agent/debugged_process.h"
#include "garnet/public/lib/fxl/logging.h"

ProcessBreakpoint::ProcessBreakpoint(DebuggedProcess* process)
    : process_(process) {}

ProcessBreakpoint::~ProcessBreakpoint() {
  Uninstall();
}

bool ProcessBreakpoint::SetSettings(
    const debug_ipc::BreakpointSettings& settings) {
  if (settings.address != settings_.address) {
    Uninstall();
    settings_ = settings;
    return Install();
  }
  settings_ = settings;
  return true;
}

void ProcessBreakpoint::FixupMemoryBlock(debug_ipc::MemoryBlock* block) {
  if (block->data.empty())
    return;  // Nothing to do.
  FXL_DCHECK(static_cast<size_t>(block->size) == block->data.size());

  size_t src_size = sizeof(arch::BreakInstructionType);
  const uint8_t* src = reinterpret_cast<uint8_t*>(&previous_data_);

  // Simple implementation to prevent boundary errors (ARM instructions are
  // 32-bits and could be hanging partially off either end of the requested
  // buffer).
  for (size_t i = 0; i < src_size; i++) {
    uint64_t dest_address = address() + i;
    if (dest_address >= block->address &&
        dest_address < block->address + block->size)
      block->data[dest_address - block->address] = src[i];
  }
}

bool ProcessBreakpoint::Install() {
  FXL_DCHECK(!installed_);

  // Read previous instruction contents.
  size_t actual = 0;
  zx_status_t status = process_->process().read_memory(
      address(), &previous_data_, sizeof(arch::BreakInstructionType), &actual);
  if (status != ZX_OK || actual != sizeof(arch::BreakInstructionType))
    return false;

  // Replace with breakpoint instruction.
  status = process_->process().write_memory(
      address(), &arch::kBreakInstruction,
      sizeof(arch::BreakInstructionType), &actual);
  if (status != ZX_OK || actual != sizeof(arch::BreakInstructionType))
    return false;
  installed_ = true;
  return true;
}

void ProcessBreakpoint::Uninstall() {
  if (!installed_)
    return;  // Not installed.

  // If the breakpoint was previously installed it means the memory address
  // was valid and writable, so we generally expect to be able to do the same
  // write to uninstall it. But it could have been unmapped during execution
  // or even remapped with something else. So verify that it's still a
  // breakpoint instruction before doing any writes.
  arch::BreakInstructionType current_contents = 0;
  size_t actual = 0;
  zx_status_t status = process_->process().read_memory(
      address(), &current_contents, sizeof(arch::BreakInstructionType),
      &actual);
  if (status != ZX_OK || actual != sizeof(arch::BreakInstructionType))
    return;  // Probably unmapped, safe to ignore.

  if (current_contents != arch::kBreakInstruction) {
    fprintf(stderr, "Warning: Debug break instruction unexpectedly replaced "
            "at %" PRIX64 "\n",
            address());
    return;  // Replaced with something else, ignore.
  }

  status = process_->process().write_memory(
      address(), &previous_data_, sizeof(arch::BreakInstructionType), &actual);
  if (status != ZX_OK || actual != sizeof(arch::BreakInstructionType)) {
    fprintf(stderr, "Warning: unable to remove breakpoint at %" PRIX64 ".",
            address());
  }
  installed_ = false;
}
