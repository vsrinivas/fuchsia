// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "breakpoint.h"

#include <cinttypes>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

namespace debugserver {
namespace arch {

Breakpoint::Breakpoint(uintptr_t address, size_t kind, BreakpointSet* owner)
    : address_(address), kind_(kind), owner_(owner) {
  FTL_DCHECK(owner_);
}

SoftwareBreakpoint::SoftwareBreakpoint(uintptr_t address,
                                       size_t kind,
                                       BreakpointSet* owner)
    : Breakpoint(address, kind, owner) {}

SoftwareBreakpoint::~SoftwareBreakpoint() {
  if (IsInserted())
    Remove();
}

BreakpointSet::BreakpointSet(Process* process) : process_(process) {
  FTL_DCHECK(process_);
}

bool BreakpointSet::InsertSoftwareBreakpoint(uintptr_t address, size_t kind) {
  if (breakpoints_.find(address) != breakpoints_.end()) {
    FTL_LOG(ERROR) << ftl::StringPrintf(
        "Breakpoint already inserted at address: 0x%" PRIxPTR, address);
    return false;
  }

  std::unique_ptr<Breakpoint> breakpoint(
      new SoftwareBreakpoint(address, kind, this));
  if (!breakpoint->Insert()) {
    FTL_LOG(ERROR) << "Failed to insert software breakpoint";
    return false;
  }

  breakpoints_[address] = std::move(breakpoint);
  return true;
}

bool BreakpointSet::RemoveSoftwareBreakpoint(uintptr_t address) {
  auto iter = breakpoints_.find(address);
  if (iter == breakpoints_.end()) {
    FTL_LOG(ERROR) << ftl::StringPrintf(
        "No breakpoint inserted at address: 0x%" PRIxPTR, address);
    return false;
  }

  if (!iter->second->Remove()) {
    FTL_LOG(ERROR) << "Failed to remove breakpoint";
    return false;
  }

  breakpoints_.erase(iter);
  return true;
}

}  // namespace arch
}  // namespace debugserver
