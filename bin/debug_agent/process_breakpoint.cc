// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/process_breakpoint.h"

#include <inttypes.h>
#include <zircon/syscalls/exception.h>

#include <algorithm>

#include "garnet/bin/debug_agent/breakpoint.h"
#include "lib/fxl/logging.h"

namespace debug_agent {

ProcessBreakpoint::ProcessBreakpoint(Breakpoint* breakpoint,
                                     ProcessMemoryAccessor* memory_accessor,
                                     zx_koid_t process_koid, uint64_t address)
    : memory_accessor_(memory_accessor),
      process_koid_(process_koid),
      address_(address) {
  breakpoints_.push_back(breakpoint);
}

ProcessBreakpoint::~ProcessBreakpoint() { Uninstall(); }

zx_status_t ProcessBreakpoint::Init() { return Install(); }

void ProcessBreakpoint::RegisterBreakpoint(Breakpoint* breakpoint) {
  // Shouldn't get duplicates.
  FXL_DCHECK(std::find(breakpoints_.begin(), breakpoints_.end(), breakpoint) ==
             breakpoints_.end());
  breakpoints_.push_back(breakpoint);
}

bool ProcessBreakpoint::UnregisterBreakpoint(Breakpoint* breakpoint) {
  auto found = std::find(breakpoints_.begin(), breakpoints_.end(), breakpoint);
  if (found == breakpoints_.end()) {
    FXL_NOTREACHED();  // Should always be found.
  } else {
    breakpoints_.erase(found);
  }
  return !breakpoints_.empty();
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

void ProcessBreakpoint::OnHit(
    std::vector<debug_ipc::BreakpointStats>* hit_breakpoints) {
  hit_breakpoints->resize(breakpoints_.size());
  for (size_t i = 0; i < breakpoints_.size(); i++) {
    breakpoints_[i]->OnHit();
    (*hit_breakpoints)[i] = breakpoints_[i]->stats();
  }
}

void ProcessBreakpoint::BeginStepOver(zx_koid_t thread_koid) {
  // Should't be recursively stepping over a breakpoint from the same thread.
  FXL_DCHECK(thread_step_over_.find(thread_koid) == thread_step_over_.end());

  if (!CurrentlySteppingOver()) {
    // This is the first thread to attempt to step over the breakpoint (there
    // could theoretically be more than one).
    Uninstall();
  }
  thread_step_over_[thread_koid] = StepStatus::kCurrent;
}

bool ProcessBreakpoint::BreakpointStepHasException(zx_koid_t thread_koid,
                                                   uint32_t exception_type) {
  auto found_thread = thread_step_over_.find(thread_koid);
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

zx_status_t ProcessBreakpoint::Install() {
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
  status = memory_accessor_->WriteProcessMemory(
      address(), &arch::kBreakInstruction, sizeof(arch::BreakInstructionType),
      &actual);
  if (status != ZX_OK)
    return status;
  if (actual != sizeof(arch::BreakInstructionType))
    return ZX_ERR_UNAVAILABLE;

  installed_ = true;
  return ZX_OK;
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
  zx_status_t status = memory_accessor_->ReadProcessMemory(
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

  status = memory_accessor_->WriteProcessMemory(
      address(), &previous_data_, sizeof(arch::BreakInstructionType), &actual);
  if (status != ZX_OK || actual != sizeof(arch::BreakInstructionType)) {
    fprintf(stderr, "Warning: unable to remove breakpoint at %" PRIX64 ".",
            address());
  }
  installed_ = false;
}

}  // namespace debug_agent
