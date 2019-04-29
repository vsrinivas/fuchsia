// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/watchpoint.h"

#include "src/developer/debug/ipc/records_utils.h"
#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

Watchpoint::Watchpoint(ProcessDelegate* delegate) : delegate_(delegate) {}

Watchpoint::~Watchpoint() {
  for (auto& [process_koid, range] : installed_watchpoints_) {
    delegate_->UnregisterWatchpoint(this, process_koid, range);
  }
}

zx_status_t Watchpoint::SetSettings(
    const debug_ipc::WatchpointSettings& settings) {
  zx_status_t result = ZX_OK;
  settings_ = settings;
  stats_.id = settings_.id;

  // The updated set of locations.
  std::set<WatchpointInstallation> updated_locations;
  for (const auto& cur : settings.locations) {
    WatchpointInstallation installation = {cur.process_koid, cur.range};
    updated_locations.insert(std::move(installation));
  }

  // Removed locations.
  for (const auto& loc : installed_watchpoints_) {
    if (updated_locations.find(loc) == updated_locations.end())
      delegate_->UnregisterWatchpoint(this, loc.process_koid, loc.range);
  }

  // Added locations.
  for (const auto& loc : updated_locations) {
    if (installed_watchpoints_.find(loc) == installed_watchpoints_.end()) {
      zx_status_t process_status =
          delegate_->RegisterWatchpoint(this, loc.process_koid, loc.range);
      if (process_status != ZX_OK)
        result = process_status;
    }
  }

  installed_watchpoints_ = std::move(updated_locations);
  return result;
}

bool Watchpoint::ThreadsToInstall(zx_koid_t process_koid,
                                  std::set<zx_koid_t>* out) const {
  std::set<zx_koid_t> thread_koids = {};
  for (const auto& location : settings_.locations) {
    if (location.process_koid != process_koid)
      continue;

    // |thread_koid| == 0 means all the threads.
    if (location.thread_koid == 0) {
      *out = {};
      return true;
    }

    thread_koids.insert(location.thread_koid);
  }

  if (thread_koids.empty())
    return false;

  *out = std::move(thread_koids);
  return true;
}

bool Watchpoint::WatchpointInstallation::operator<(
    const WatchpointInstallation& other) const {
  if (process_koid != other.process_koid)
    return process_koid < other.process_koid;

  debug_ipc::AddressRangeCompare comparer;
  return comparer(range, other.range);
}

debug_ipc::BreakpointStats Watchpoint::OnHit() {
  stats_.hit_count++;
  if (settings_.one_shot)
    stats_.should_delete = true;

  return stats_;
}

}  // namespace debug_agent
