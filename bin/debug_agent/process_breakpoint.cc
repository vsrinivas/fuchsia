// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/process_breakpoint.h"

#include <inttypes.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include "garnet/bin/debug_agent/debugged_process.h"
#include "garnet/bin/debug_agent/debugged_thread.h"
#include "garnet/public/lib/fxl/logging.h"

namespace debug_agent {

ProcessBreakpoint::ProcessBreakpoint(DebuggedProcess* process)
    : process_(process) {}

ProcessBreakpoint::~ProcessBreakpoint() {
  Uninstall();
}

bool ProcessBreakpoint::SetSettings(
    const debug_ipc::BreakpointSettings& settings) {
  if (settings.address != settings_.address) {
    if (CurrentlySteppingOver()) {
      // No threads currently stepping over this breakpoint, can just remove.
      Uninstall();
    } else {
      // There are threads currently stepping over this breakpoint and the
      // breakpoint isn't currently installed, so we can't uninstall it.
      // Instead, mark the threads currently waiting on a single step as
      // being obsolete so they will not try to put back the breakpoint.
      for (auto& pair : thread_step_over_)
        pair.second = StepStatus::kObsolete;
    }
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

void ProcessBreakpoint::BeginStepOver(DebuggedThread* thread) {
  // Should't be recursively stepping over a breakpoint from the same thread.
  FXL_DCHECK(thread_step_over_.find(thread) == thread_step_over_.end());

  if (!CurrentlySteppingOver()) {
    // This is the first thread to attempt to step over the breakpoint (there
    // could theoretically be more than one).
    Uninstall();
  }
  thread_step_over_[thread] = StepStatus::kCurrent;
}

bool ProcessBreakpoint::BreakpointStepHasException(DebuggedThread* thread,
                                                   uint32_t exception_type) {
  auto found_thread = thread_step_over_.find(thread);
  if (found_thread == thread_step_over_.end()) {
    // Shouldn't be getting these notifications from a thread not currently
    // doing a step-over.
    FXL_NOTREACHED();
    return false;
  }
  StepStatus step_status = found_thread->second;
  thread_step_over_.erase(found_thread);

  // When the last thread is done stepping over, put the breakpoing back.
  if (step_status == StepStatus::kCurrent && !CurrentlySteppingOver())
    Install();

  // Now check if this exception was likely caused by successfully stepping
  // over the breakpoint (hardware breakpoint), or something else (the stepped
  // instruction crashed or something).
  return exception_type == ZX_EXCP_HW_BREAKPOINT;
}

bool ProcessBreakpoint::CurrentlySteppingOver() const {
  for (const auto& pair : thread_step_over_) {
    if (pair.second == StepStatus::kCurrent)
      return true;
  }
  return false;
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
  status = process_->process().write_memory(address(), &arch::kBreakInstruction,
                                            sizeof(arch::BreakInstructionType),
                                            &actual);
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
    fprintf(stderr,
            "Warning: Debug break instruction unexpectedly replaced "
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

}  // namespace debug_agent
