// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/breakpoint.h"

#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

namespace {

// Debug logging to see if a breakpoint applies to a thread.
void LogAppliesToThread(uint32_t breakpoint_id, zx_koid_t pid, zx_koid_t tid,
                        bool applies) {
  DEBUG_LOG(Breakpoint) << "Breakpoint " << breakpoint_id
                        << " applies to [P: " << pid << ", T: " << tid << "]? "
                        << applies;
}

}  // namespace

Breakpoint::Breakpoint(ProcessDelegate* process_delegate)
    : process_delegate_(process_delegate) {}

Breakpoint::~Breakpoint() {
  for (const auto& loc : locations_)
    process_delegate_->UnregisterBreakpoint(this, loc.first, loc.second);
}

zx_status_t Breakpoint::SetSettings(
    const debug_ipc::BreakpointSettings& settings) {
  zx_status_t result = ZX_OK;
  settings_ = settings;

  // The stats needs to reference the current ID. We assume setting the
  // settings doesn't update the stats (an option to do this may need to be
  // added in the future).
  stats_.breakpoint_id = settings_.breakpoint_id;

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
      if (process_status != ZX_OK)
        result = process_status;
    }
  }

  locations_ = std::move(new_set);
  return result;
}



bool Breakpoint::AppliesToThread(zx_koid_t pid,
                                 zx_koid_t tid) const {
  for (auto& location : settings_.locations) {
    if (location.process_koid == pid) {
      if (location.thread_koid == 0 || location.thread_koid == tid) {
        LogAppliesToThread(settings_.breakpoint_id, pid, tid, true);
        return true;
      }
    }
  }

  LogAppliesToThread(settings_.breakpoint_id, pid, tid, false);
  return false;
}

// In the future we will want to have breakpoints that trigger on a specific
// hit count or other conditions and will need a "kContinue" result.
Breakpoint::HitResult Breakpoint::OnHit() {
  stats_.hit_count++;
  if (settings_.one_shot) {
    stats_.should_delete = true;
    return HitResult::kOneShotHit;
  }
  return HitResult::kHit;
}

}  // namespace debug_agent
