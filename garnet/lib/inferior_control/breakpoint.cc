// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cinttypes>

#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>

#include "garnet/lib/debugger_utils/breakpoints.h"

#include "breakpoint.h"
#include "process.h"

namespace inferior_control {

Breakpoint::Breakpoint(zx_vaddr_t address, size_t size)
    : address_(address), size_(size) {}

ProcessBreakpoint::ProcessBreakpoint(zx_vaddr_t address, size_t size,
                                     ProcessBreakpointSet* owner)
    : Breakpoint(address, size), owner_(owner) {
  FXL_DCHECK(owner_);
}

SoftwareBreakpoint::SoftwareBreakpoint(zx_vaddr_t address,
                                       ProcessBreakpointSet* owner)
    : ProcessBreakpoint(address, Size(), owner) {}

SoftwareBreakpoint::~SoftwareBreakpoint() {
  if (IsInserted())
    Remove();
}

size_t SoftwareBreakpoint::Size() {
  return debugger_utils::GetBreakpointInstructionSize();
}

bool SoftwareBreakpoint::Insert() {
  // TODO(PT-103): Handle breakpoints in unloaded solibs.

  if (IsInserted()) {
    FXL_LOG(WARNING) << "Breakpoint already inserted";
    return false;
  }

  // Read the current contents at the address that we're about to overwrite, so
  // that it can be restored later.
  size_t num_bytes = Size();
  std::vector<uint8_t> orig;
  orig.resize(num_bytes);
  if (!owner()->process()->ReadMemory(address(), orig.data(), num_bytes)) {
    FXL_LOG(ERROR) << "Failed to obtain current contents of memory";
    return false;
  }

  // Insert the breakpoint instruction.
  const uint8_t* insn = debugger_utils::GetBreakpointInstruction();
  if (!owner()->process()->WriteMemory(address(), insn, num_bytes)) {
    FXL_LOG(ERROR) << "Failed to insert software breakpoint";
    return false;
  }

  original_bytes_ = std::move(orig);
  return true;
}

bool SoftwareBreakpoint::Remove() {
  if (!IsInserted()) {
    FXL_LOG(WARNING) << "Breakpoint not inserted";
    return false;
  }

  FXL_DCHECK(original_bytes_.size() == Size());

  // Restore the original contents.
  if (!owner()->process()->WriteMemory(
        address(), original_bytes_.data(), Size())) {
    FXL_LOG(ERROR) << "Failed to restore original instructions";
    return false;
  }

  original_bytes_.clear();
  return true;
}

bool SoftwareBreakpoint::IsInserted() const {
  return !original_bytes_.empty();
}

ProcessBreakpointSet::ProcessBreakpointSet(Process* process)
    : process_(process) {
  FXL_DCHECK(process_);
}

bool ProcessBreakpointSet::InsertSoftwareBreakpoint(zx_vaddr_t address) {
  if (breakpoints_.find(address) != breakpoints_.end()) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
        "Breakpoint already inserted at address: 0x%" PRIxPTR, address);
    return false;
  }

  std::unique_ptr<ProcessBreakpoint> breakpoint(
      new SoftwareBreakpoint(address, this));
  if (!breakpoint->Insert()) {
    FXL_LOG(ERROR) << "Failed to insert software breakpoint";
    return false;
  }

  breakpoints_[address] = std::move(breakpoint);
  return true;
}

bool ProcessBreakpointSet::RemoveSoftwareBreakpoint(zx_vaddr_t address) {
  auto iter = breakpoints_.find(address);
  if (iter == breakpoints_.end()) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
        "No breakpoint inserted at address: 0x%" PRIxPTR, address);
    return false;
  }

  if (!iter->second->Remove()) {
    FXL_LOG(ERROR) << "Failed to remove breakpoint";
    return false;
  }

  breakpoints_.erase(iter);
  return true;
}

ThreadBreakpoint::ThreadBreakpoint(zx_vaddr_t address, size_t size,
                                   ThreadBreakpointSet* owner)
    : Breakpoint(address, size), owner_(owner) {
  FXL_DCHECK(owner_);
}

SingleStepBreakpoint::SingleStepBreakpoint(zx_vaddr_t address,
                                           ThreadBreakpointSet* owner)
    : ThreadBreakpoint(address, 0 /*TODO:type?*/, owner) {}

SingleStepBreakpoint::~SingleStepBreakpoint() {
  if (IsInserted())
    Remove();
}

ThreadBreakpointSet::ThreadBreakpointSet(Thread* thread) : thread_(thread) {
  FXL_DCHECK(thread_);
}

bool ThreadBreakpointSet::InsertSingleStepBreakpoint(zx_vaddr_t address) {
  if (single_step_breakpoint_) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
        "S/S bkpt already inserted at 0x%" PRIxPTR
        ", requested address: 0x%" PRIxPTR,
        single_step_breakpoint_->address(), address);
    return false;
  }

  std::unique_ptr<ThreadBreakpoint> breakpoint(
      new SingleStepBreakpoint(address, this));
  if (!breakpoint->Insert()) {
    FXL_LOG(ERROR) << "Failed to insert s/s bkpt";
    return false;
  }

  single_step_breakpoint_ = std::move(breakpoint);
  return true;
}

bool ThreadBreakpointSet::RemoveSingleStepBreakpoint() {
  if (!single_step_breakpoint_) {
    FXL_LOG(ERROR) << fxl::StringPrintf("No s/s bkpt inserted");
    return false;
  }

  single_step_breakpoint_.reset();
  return true;
}

bool ThreadBreakpointSet::SingleStepBreakpointInserted() {
  return !!single_step_breakpoint_;
}

}  // namespace inferior_control
