// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "breakpoint.h"

#include <cinttypes>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace debugserver {

Breakpoint::Breakpoint(uintptr_t address, size_t kind)
    : address_(address), kind_(kind) {}

ProcessBreakpoint::ProcessBreakpoint(uintptr_t address, size_t kind,
                                     ProcessBreakpointSet* owner)
    : Breakpoint(address, kind), owner_(owner) {
  FXL_DCHECK(owner_);
}

SoftwareBreakpoint::SoftwareBreakpoint(uintptr_t address, size_t kind,
                                       ProcessBreakpointSet* owner)
    : ProcessBreakpoint(address, kind, owner) {}

SoftwareBreakpoint::~SoftwareBreakpoint() {
  if (IsInserted()) Remove();
}

ProcessBreakpointSet::ProcessBreakpointSet(Process* process)
    : process_(process) {
  FXL_DCHECK(process_);
}

bool ProcessBreakpointSet::InsertSoftwareBreakpoint(uintptr_t address,
                                                    size_t kind) {
  if (breakpoints_.find(address) != breakpoints_.end()) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
        "Breakpoint already inserted at address: 0x%" PRIxPTR, address);
    return false;
  }

  std::unique_ptr<ProcessBreakpoint> breakpoint(
      new SoftwareBreakpoint(address, kind, this));
  if (!breakpoint->Insert()) {
    FXL_LOG(ERROR) << "Failed to insert software breakpoint";
    return false;
  }

  breakpoints_[address] = std::move(breakpoint);
  return true;
}

bool ProcessBreakpointSet::RemoveSoftwareBreakpoint(uintptr_t address) {
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

ThreadBreakpoint::ThreadBreakpoint(uintptr_t address, size_t kind,
                                   ThreadBreakpointSet* owner)
    : Breakpoint(address, kind), owner_(owner) {
  FXL_DCHECK(owner_);
}

SingleStepBreakpoint::SingleStepBreakpoint(uintptr_t address,
                                           ThreadBreakpointSet* owner)
    : ThreadBreakpoint(address, 0 /*TODO:type?*/, owner) {}

SingleStepBreakpoint::~SingleStepBreakpoint() {
  if (IsInserted()) Remove();
}

ThreadBreakpointSet::ThreadBreakpointSet(Thread* thread) : thread_(thread) {
  FXL_DCHECK(thread_);
}

bool ThreadBreakpointSet::InsertSingleStepBreakpoint(uintptr_t address) {
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

}  // namespace debugserver
