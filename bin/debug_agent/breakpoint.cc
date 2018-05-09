// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/breakpoint.h"

#include "garnet/bin/debug_agent/process_breakpoint.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace debug_agent {

Breakpoint::Breakpoint(ProcessDelegate* process_delegate)
    : process_delegate_(process_delegate) {}
Breakpoint::~Breakpoint() = default;

zx_status_t Breakpoint::SetSettings(
    const debug_ipc::BreakpointSettings& settings) {
  zx_status_t result = ZX_OK;
  settings_ = settings;

  // The set of new locations.
  std::set<LocationPair> new_set;
  for (const auto& cur : settings.locations)
    new_set.emplace(cur.process_koid, cur.address);

  // Removed locations.
  for (const auto& loc : locations_) {
    if (new_set.find(loc) == new_set.end())
      process_delegate_->UnregisterBreakpoint(this, loc.first, loc.second);
  }

  // Added locations.
  for (const auto& loc : new_set) {
    if (locations_.find(loc) == locations_.end()) {
      zx_status_t process_status =
          process_delegate_->RegisterBreakpoint(this, loc.first, loc.second);
      if (process_status != ZX_OK) result = process_status;
    }
  }

  locations_ = std::move(new_set);
  return result;
}

}  // namespace debug_agent
