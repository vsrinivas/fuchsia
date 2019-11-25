// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/breakpoint.h"

#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

// Breakpoint::ProcessDelegate ---------------------------------------------------------------------

zx_status_t Breakpoint::ProcessDelegate::RegisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid,
                                                            uint64_t address) {
  FXL_NOTREACHED() << "Should override.";
  return ZX_ERR_NOT_SUPPORTED;
}

// Called When the breakpoint no longer applies to this location.
void Breakpoint::ProcessDelegate::UnregisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid,
                                                       uint64_t address) {
  FXL_NOTREACHED() << "Should override.";
}

zx_status_t Breakpoint::ProcessDelegate::RegisterWatchpoint(Breakpoint* bp, zx_koid_t process_koid,
                                                            const debug_ipc::AddressRange& range) {
  FXL_NOTREACHED() << "Should override.";
  return ZX_ERR_NOT_SUPPORTED;
}

void Breakpoint::ProcessDelegate::UnregisterWatchpoint(Breakpoint* bp, zx_koid_t process_koid,
                                                       const debug_ipc::AddressRange& range) {
  FXL_NOTREACHED() << "Should override.";
}

// Breakpoint --------------------------------------------------------------------------------------

namespace {

std::string Preamble(const Breakpoint* bp) {
  return fxl::StringPrintf("[Breakpoint %u (%s)] ", bp->settings().id, bp->settings().name.c_str());
}

// Debug logging to see if a breakpoint applies to a thread.
void LogAppliesToThread(const Breakpoint* bp, zx_koid_t pid, zx_koid_t tid, bool applies) {
  DEBUG_LOG(Breakpoint) << Preamble(bp) << "applies to [P: " << pid << ", T: " << tid << "]? "
                        << applies;
}

void LogSetSettings(debug_ipc::FileLineFunction location, const Breakpoint* bp) {
  std::stringstream ss;

  // Print a list of locations (process + thread + address) place of an actual breakpoint.
  ss << "Updating locations: ";
  for (auto& location : bp->settings().locations) {
    // Log the process.
    ss << std::dec << "[P: " << location.process_koid;

    // |thread_koid| == 0 means that it applies to all the threads.
    if (location.thread_koid != 0)
      ss << ", T: " << location.thread_koid;

    // Print the actual location.
    ss << ", 0x" << std::hex << location.address << "] ";
    ss << ", range: " << location.address_range.ToString();
  }

  DEBUG_LOG_WITH_LOCATION(Breakpoint, location) << Preamble(bp) << ss.str();
}

}  // namespace

Breakpoint::Breakpoint(ProcessDelegate* process_delegate) : process_delegate_(process_delegate) {}

Breakpoint::~Breakpoint() {
  DEBUG_LOG(Breakpoint) << Preamble(this) << "Deleting.";
  for (const auto& loc : locations_)
    process_delegate_->UnregisterBreakpoint(this, loc.first, loc.second);
}

zx_status_t Breakpoint::SetSettings(debug_ipc::BreakpointType type,
                                    const debug_ipc::BreakpointSettings& settings) {
  FXL_DCHECK(type != debug_ipc::BreakpointType::kLast);
  type_ = type;
  settings_ = settings;
  LogSetSettings(FROM_HERE, this);

  // The stats needs to reference the current ID. We assume setting the
  // settings doesn't update the stats (an option to do this may need to be
  // added in the future).
  stats_.id = settings_.id;

  switch (type) {
    case debug_ipc::BreakpointType::kSoftware:
    case debug_ipc::BreakpointType::kHardware:
      return SetBreakpointLocations(settings);
    case debug_ipc::BreakpointType::kWatchpoint:
      return SetWatchpointLocations(settings);
    case debug_ipc::BreakpointType::kLast:
      break;
  }

  FXL_NOTREACHED() << "Invalid breakpoint type: " << static_cast<int>(type);
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t Breakpoint::SetBreakpointLocations(const debug_ipc::BreakpointSettings& settings) {
  zx_status_t result = ZX_OK;

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

zx_status_t Breakpoint::SetWatchpointLocations(const debug_ipc::BreakpointSettings& settings) {
  zx_status_t result = ZX_OK;

  // The set of new locations.
  std::set<WatchpointLocationPair, WatchpointLocationPairCompare> new_set;
  for (const auto& cur : settings.locations)
    new_set.emplace(cur.process_koid, cur.address_range);

  // Removed locations.
  for (const auto& loc : watchpoint_locations_) {
    if (new_set.find(loc) == new_set.end())
      process_delegate_->UnregisterWatchpoint(this, loc.first, loc.second);
  }

  // Added locations.
  for (const auto& loc : new_set) {
    if (watchpoint_locations_.find(loc) == watchpoint_locations_.end()) {
      zx_status_t process_status =
          process_delegate_->RegisterWatchpoint(this, loc.first, loc.second);
      if (process_status != ZX_OK)
        result = process_status;
    }
  }

  watchpoint_locations_ = std::move(new_set);
  return result;
}

bool Breakpoint::AppliesToThread(zx_koid_t pid, zx_koid_t tid) const {
  for (auto& location : settings_.locations) {
    if (location.process_koid == pid) {
      if (location.thread_koid == 0 || location.thread_koid == tid) {
        LogAppliesToThread(this, pid, tid, true);
        return true;
      }
    }
  }

  LogAppliesToThread(this, pid, tid, false);
  return false;
}

// In the future we will want to have breakpoints that trigger on a specific
// hit count or other conditions and will need a "kContinue" result.
Breakpoint::HitResult Breakpoint::OnHit() {
  stats_.hit_count++;
  if (settings_.one_shot) {
    DEBUG_LOG(Breakpoint) << Preamble(this) << "One-shot breakpoint. Will be deleted.";
    stats_.should_delete = true;
    return HitResult::kOneShotHit;
  }
  return HitResult::kHit;
}

// WatchpointLocationPairCompare -------------------------------------------------------------------

bool Breakpoint::WatchpointLocationPairCompare::operator()(
    const WatchpointLocationPair& lhs, const WatchpointLocationPair& rhs) const {
  if (lhs.first != rhs.first)
    return lhs.first < rhs.first;

  if (lhs.second.begin() != rhs.second.begin())
    return lhs.second.begin() < rhs.second.begin();
  return lhs.second.end() < rhs.second.end();
}

}  // namespace debug_agent
