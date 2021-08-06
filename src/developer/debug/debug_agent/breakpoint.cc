// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/breakpoint.h"

#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

// Breakpoint::DoesExceptionApply ------------------------------------------------------------------

bool Breakpoint::DoesExceptionApply(debug_ipc::BreakpointType exception_type,
                                    debug_ipc::BreakpointType bp_type) {
  if (exception_type == debug_ipc::BreakpointType::kLast ||
      bp_type == debug_ipc::BreakpointType::kLast) {
    FX_NOTREACHED() << "Wrong exception (" << static_cast<uint32_t>(exception_type)
                    << ") or bp_type (" << static_cast<uint32_t>(bp_type) << ").";
    return false;
  }

  if (exception_type == debug_ipc::BreakpointType::kSoftware)
    return bp_type == debug_ipc::BreakpointType::kSoftware;

  if (exception_type == debug_ipc::BreakpointType::kHardware)
    return bp_type == debug_ipc::BreakpointType::kHardware;

  // Now only watchpoint types are left.
  if (!IsWatchpointType(bp_type))
    return false;

  // If any the types is a read write, it targets this type.
  if (exception_type == debug_ipc::BreakpointType::kReadWrite ||
      bp_type == debug_ipc::BreakpointType::kReadWrite)
    return true;

  // R/W case are already covered.
  return exception_type == bp_type;
}

// Breakpoint::ProcessDelegate ---------------------------------------------------------------------

debug::Status Breakpoint::ProcessDelegate::RegisterBreakpoint(Breakpoint* bp,
                                                              zx_koid_t process_koid,
                                                              uint64_t address) {
  FX_NOTREACHED() << "Should override.";
  return debug::Status("Expecting override.");
}

// Called When the breakpoint no longer applies to this location.
void Breakpoint::ProcessDelegate::UnregisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid,
                                                       uint64_t address) {
  FX_NOTREACHED() << "Should override.";
}

debug::Status Breakpoint::ProcessDelegate::RegisterWatchpoint(Breakpoint* bp,
                                                              zx_koid_t process_koid,
                                                              const debug::AddressRange& range) {
  FX_NOTREACHED() << "Should override.";
  return debug::Status("Expecting override.");
}

void Breakpoint::ProcessDelegate::UnregisterWatchpoint(Breakpoint* bp, zx_koid_t process_koid,
                                                       const debug::AddressRange& range) {
  FX_NOTREACHED() << "Should override.";
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

void LogSetSettings(debug::FileLineFunction location, const Breakpoint* bp) {
  std::stringstream ss;

  // Print a list of locations (process + thread + address) place of an actual breakpoint.
  ss << "Updating locations: ";
  for (auto& location : bp->settings().locations) {
    // Log the process.
    ss << std::dec << "[P: " << location.id.process;

    // |thread_koid| == 0 means that it applies to all the threads.
    if (location.id.thread != 0)
      ss << ", T: " << location.id.thread;

    // Print the actual location.
    ss << "], addr: 0x" << std::hex << location.address
       << ", range: " << location.address_range.ToString();
  }

  DEBUG_LOG_WITH_LOCATION(Breakpoint, location) << Preamble(bp) << ss.str();
}

}  // namespace

Breakpoint::Breakpoint(ProcessDelegate* process_delegate, bool is_debug_agent_internal)
    : process_delegate_(process_delegate), is_debug_agent_internal_(is_debug_agent_internal) {}

Breakpoint::~Breakpoint() {
  DEBUG_LOG(Breakpoint) << Preamble(this) << "Deleting.";
  for (const auto& [process_koid, address] : locations_) {
    DEBUG_LOG(Breakpoint) << "- Proc " << process_koid << " at address 0x " << std::hex << address;
    process_delegate_->UnregisterBreakpoint(this, process_koid, address);
  }

  for (const auto& [process_koid, address_range] : watchpoint_locations_) {
    DEBUG_LOG(Breakpoint) << "- Proc " << process_koid << " at address " << std::hex
                          << address_range.ToString();
    process_delegate_->UnregisterWatchpoint(this, process_koid, address_range);
  }
}

debug::Status Breakpoint::SetSettings(const debug_ipc::BreakpointSettings& settings) {
  FX_DCHECK(settings.type != debug_ipc::BreakpointType::kLast);
  settings_ = settings;
  LogSetSettings(FROM_HERE, this);

  // The stats needs to reference the current ID. We assume setting the
  // settings doesn't update the stats (an option to do this may need to be
  // added in the future).
  stats_.id = settings_.id;

  switch (settings_.type) {
    case debug_ipc::BreakpointType::kSoftware:
    case debug_ipc::BreakpointType::kHardware:
      return SetBreakpointLocations(settings);
    case debug_ipc::BreakpointType::kReadWrite:
    case debug_ipc::BreakpointType::kWrite:
      return SetWatchpointLocations(settings);
    case debug_ipc::BreakpointType::kLast:
      break;
  }

  FX_NOTREACHED() << "Invalid breakpoint type: " << static_cast<int>(settings_.type);
  return debug::Status("Invalid breakpoint type");
}

debug::Status Breakpoint::SetSettings(std::string name, zx_koid_t process_koid, uint64_t address) {
  debug_ipc::BreakpointSettings settings;
  settings.id = debug_ipc::kDebugAgentInternalBreakpointId;
  settings.name = std::move(name);

  debug_ipc::ProcessBreakpointSettings& location = settings.locations.emplace_back();
  location.id.process = process_koid;
  location.address = address;

  return SetSettings(settings);
}

debug::Status Breakpoint::SetBreakpointLocations(const debug_ipc::BreakpointSettings& settings) {
  debug::Status result;

  // The set of new locations.
  std::set<LocationPair> new_set;
  for (const auto& cur : settings.locations)
    new_set.emplace(cur.id.process, cur.address);

  // Removed locations.
  for (const auto& loc : locations_) {
    if (new_set.find(loc) == new_set.end())
      process_delegate_->UnregisterBreakpoint(this, loc.first, loc.second);
  }

  // Added locations.
  for (const auto& loc : new_set) {
    if (locations_.find(loc) == locations_.end()) {
      debug::Status process_status =
          process_delegate_->RegisterBreakpoint(this, loc.first, loc.second);
      if (process_status.has_error())
        result = process_status;
    }
  }

  locations_ = std::move(new_set);
  return result;
}

debug::Status Breakpoint::SetWatchpointLocations(const debug_ipc::BreakpointSettings& settings) {
  debug::Status result;

  // The set of new locations.
  std::set<WatchpointLocationPair, WatchpointLocationPairCompare> new_set;
  for (const auto& cur : settings.locations)
    new_set.emplace(cur.id.process, cur.address_range);

  // Removed locations.
  for (const auto& loc : watchpoint_locations_) {
    if (new_set.find(loc) == new_set.end())
      process_delegate_->UnregisterWatchpoint(this, loc.first, loc.second);
  }

  // Added locations.
  for (const auto& loc : new_set) {
    if (watchpoint_locations_.find(loc) == watchpoint_locations_.end()) {
      debug::Status process_status =
          process_delegate_->RegisterWatchpoint(this, loc.first, loc.second);
      if (process_status.has_error())
        result = process_status;
    }
  }

  watchpoint_locations_ = std::move(new_set);
  return result;
}

bool Breakpoint::AppliesToThread(zx_koid_t pid, zx_koid_t tid) const {
  for (auto& location : settings_.locations) {
    if (location.id.process == pid) {
      if (location.id.thread == 0 || location.id.thread == tid) {
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
