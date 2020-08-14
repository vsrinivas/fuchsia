// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/breakpoint_impl.h"

#include <inttypes.h>

#include <algorithm>
#include <map>
#include <sstream>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/client/breakpoint_location_impl.h"
#include "src/developer/debug/zxdb/client/breakpoint_observer.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/permissive_input_location.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/resolve_options.h"
#include "src/developer/debug/zxdb/symbols/target_symbols.h"

namespace zxdb {

namespace {

uint32_t next_breakpoint_id = 1;

debug_ipc::Stop SettingsStopToIpcStop(BreakpointSettings::StopMode mode) {
  switch (mode) {
    case BreakpointSettings::StopMode::kNone:
      return debug_ipc::Stop::kNone;
    case BreakpointSettings::StopMode::kThread:
      return debug_ipc::Stop::kThread;
    case BreakpointSettings::StopMode::kProcess:
      return debug_ipc::Stop::kProcess;
    case BreakpointSettings::StopMode::kAll:
      return debug_ipc::Stop::kAll;
  }
}

}  // namespace

struct BreakpointImpl::ProcessRecord {
  // Helper to return whether there are any enabled locations for this process.
  bool HasEnabledLocation() const {
    for (const auto& loc : locs) {
      if (loc.second.IsEnabled())
        return true;
    }
    return false;
  }

  // Helper to add a list of locations to the locs array. Returns true if anything was added (this
  // makes the call site cleaner).
  bool AddLocations(BreakpointImpl* bp, Process* process, const std::vector<Location>& locations) {
    for (const auto& loc : locations) {
      locs.emplace(std::piecewise_construct, std::forward_as_tuple(loc.address()),
                   std::forward_as_tuple(bp, process, loc.address()));
    }
    return !locations.empty();
  }

  // Set when we're registered as an observer for this process.
  bool observing = false;

  // All resolved locations indexed by address.
  std::map<uint64_t, BreakpointLocationImpl> locs;
};

BreakpointImpl::BreakpointImpl(Session* session, bool is_internal)
    : Breakpoint(session),
      is_internal_(is_internal),
      backend_id_(next_breakpoint_id++),
      impl_weak_factory_(this) {
  session->process_observers().AddObserver(this);
  session->target_observers().AddObserver(this);
}

BreakpointImpl::~BreakpointImpl() {
  if (backend_installed_ && settings_.enabled) {
    // Breakpoint was installed and the process still exists.
    settings_.enabled = false;
    SendBackendRemove();
  }

  session()->target_observers().RemoveObserver(this);
  session()->process_observers().RemoveObserver(this);
  if (registered_as_thread_observer_)
    session()->thread_observers().RemoveObserver(this);
}

BreakpointSettings BreakpointImpl::GetSettings() const { return settings_; }

void BreakpointImpl::SetSettings(const BreakpointSettings& settings) {
  settings_ = settings;

  bool changed = false;
  for (Target* target : session()->system().GetTargets()) {
    Process* process = target->GetProcess();
    if (process && CouldApplyToProcess(process))
      changed |= RegisterProcess(process);
  }

  // Add or remove thread notifications as required.
  if (settings_.scope.thread() && !registered_as_thread_observer_) {
    session()->thread_observers().AddObserver(this);
    registered_as_thread_observer_ = true;
  } else if (!settings_.scope.thread() && registered_as_thread_observer_) {
    session()->thread_observers().RemoveObserver(this);
    registered_as_thread_observer_ = false;
  }

  SyncBackend();

  if (changed && !IsInternal()) {
    for (auto& observer : session()->breakpoint_observers())
      observer.OnBreakpointMatched(this, true);
  }
}

bool BreakpointImpl::IsInternal() const { return is_internal_; }

std::vector<const BreakpointLocation*> BreakpointImpl::GetLocations() const {
  std::vector<const BreakpointLocation*> result;
  for (auto& proc : procs_) {
    for (auto& pair : proc.second.locs)
      result.push_back(&pair.second);
  }
  return result;
}

std::vector<BreakpointLocation*> BreakpointImpl::GetLocations() {
  std::vector<BreakpointLocation*> result;
  for (auto& proc : procs_) {
    for (auto& pair : proc.second.locs)
      result.push_back(&pair.second);
  }
  return result;
}

debug_ipc::BreakpointStats BreakpointImpl::GetStats() { return stats_; }

void BreakpointImpl::UpdateStats(const debug_ipc::BreakpointStats& stats) { stats_ = stats; }

void BreakpointImpl::BackendBreakpointRemoved() { backend_installed_ = false; }

void BreakpointImpl::WillDestroyTarget(Target* target) {
  if (target == settings_.scope.target()) {
    // As with threads going away, when the target goes away for a target-scoped breakpoint, convert
    // to a disabled system-wide breakpoint.
    settings_.scope = ExecutionScope();
    settings_.enabled = false;
  }
}

void BreakpointImpl::DidCreateProcess(Process* process, bool autoattached) {
  if (CouldApplyToProcess(process)) {
    if (RegisterProcess(process)) {
      SyncBackend();

      if (!IsInternal()) {
        for (auto& observer : session()->breakpoint_observers())
          observer.OnBreakpointMatched(this, false);
      }
    }
  }
}

void BreakpointImpl::WillDestroyProcess(Process* process, ProcessObserver::DestroyReason,
                                        int exit_code) {
  auto found = procs_.find(process);
  if (found == procs_.end())
    return;

  // Only need to update the backend if there was an enabled address associated with this process.
  bool send_update = found->second.HasEnabledLocation();

  // When the process exits, disable breakpoints that are entirely address-based since the addresses
  // will normally change when a process is loaded.
  if (AllLocationsAddresses())
    settings_.enabled = false;

  procs_.erase(found);

  // Needs to be done after the ProcessRecord is removed.
  if (send_update)
    SyncBackend();
}

void BreakpointImpl::DidLoadModuleSymbols(Process* process, LoadedModuleSymbols* module) {
  if (!CouldApplyToProcess(process))
    return;  // Irrelevant process.

  FindNameContext find_context(process->GetSymbols());

  ResolveOptions options = GetResolveOptions();
  bool needs_sync = false;
  for (const auto& loc : ExpandPermissiveInputLocationNames(find_context, settings_.locations)) {
    needs_sync |=
        procs_[process].AddLocations(this, process, module->ResolveInputLocation(loc, options));
  }

  if (needs_sync) {
    SyncBackend();

    if (!IsInternal()) {
      for (auto& observer : session()->breakpoint_observers())
        observer.OnBreakpointMatched(this, false);
    }
  }
}

void BreakpointImpl::WillUnloadModuleSymbols(Process* process, LoadedModuleSymbols* module) {
  // TODO(bug 42243) need to get the address range of this module and then remove all breakpoints in
  // that range.
}

void BreakpointImpl::WillDestroyThread(Thread* thread) {
  if (settings_.scope.thread() == thread) {
    // When the thread is destroyed that the breakpoint is associated with, disable the breakpoint
    // and convert to a target-scoped breakpoint. This will preserve its state without us having to
    // maintain some "defunct thread" association. The user can associate it with a new thread and
    // re-enable as desired.
    settings_.scope = ExecutionScope(thread->GetProcess()->GetTarget());
    settings_.enabled = false;

    // Don't need more thread notifications.
    FX_DCHECK(registered_as_thread_observer_);
    session()->thread_observers().RemoveObserver(this);
    registered_as_thread_observer_ = false;
  }
}

void BreakpointImpl::SyncBackend() {
  bool has_locations = HasEnabledLocation();

  if (backend_installed_ && !has_locations) {
    SendBackendRemove();
  } else if (has_locations) {
    SendBackendAddOrChange();
  }
  // Otherwise the backend doesn't know about it and we don't require anything.
}

void BreakpointImpl::SendBackendAddOrChange() {
  backend_installed_ = true;

  debug_ipc::AddOrChangeBreakpointRequest request;
  request.breakpoint.id = backend_id_;
  request.breakpoint.type = settings_.type;
  request.breakpoint.name = settings_.name;
  request.breakpoint.stop = SettingsStopToIpcStop(settings_.stop_mode);
  request.breakpoint.one_shot = settings_.one_shot;

  for (const auto& proc : procs_) {
    for (const auto& pair : proc.second.locs) {
      if (!pair.second.IsEnabled())
        continue;

      debug_ipc::ProcessBreakpointSettings addition;
      addition.process_koid = proc.first->GetKoid();

      if (settings_.scope.type() == ExecutionScope::kThread) {
        if (Thread* thread = settings_.scope.thread())
          addition.thread_koid = thread->GetKoid();
      }

      if (BreakpointSettings::TypeHasSize(settings_.type)) {
        uint64_t address = pair.second.address();
        addition.address_range = {address, address + settings_.byte_size};
      } else {
        addition.address = pair.second.address();
      }
      request.breakpoint.locations.push_back(addition);
    }
  }

  session()->remote_api()->AddOrChangeBreakpoint(
      request, [breakpoint = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::AddOrChangeBreakpointReply reply) {
        if (breakpoint)
          breakpoint->OnAddOrChangeComplete(err, std::move(reply));
      });
}

void BreakpointImpl::SendBackendRemove() {
  debug_ipc::RemoveBreakpointRequest request;
  request.breakpoint_id = backend_id_;

  session()->remote_api()->RemoveBreakpoint(
      request, [breakpoint = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::RemoveBreakpointReply reply) {
        if (breakpoint)
          breakpoint->OnRemoveComplete(err, std::move(reply));
      });

  backend_installed_ = false;
}

void BreakpointImpl::OnAddOrChangeComplete(const Err& input_err,
                                           debug_ipc::AddOrChangeBreakpointReply reply) {
  Err err = input_err;  // Could be a transport error.
  if (err.ok() && reply.status != 0) {
    // Transport succeeded but the backend failed.
    std::stringstream ss;
    ss << "System reported error " << reply.status << " ("
       << debug_ipc::ZxStatusToString(reply.status) << ")";
    if (reply.status == debug_ipc::kZxErrNoResources) {
      ss << std::endl
         << "Is this a hardware breakpoint? Check \"sys-info\" to "
            "verify the amount available within the system.";
    } else if (reply.status == debug_ipc::kZxErrNotSupported) {
      ss << std::endl
         << "This kernel command-line flag \"kernel.enable-debugging-syscalls\" is\n"
            "likely not set.";
    }
    err = Err(ss.str());
  }

  if (err.has_error()) {
    for (auto& observer : session()->breakpoint_observers())
      observer.OnBreakpointUpdateFailure(this, err);
  }
}

void BreakpointImpl::OnRemoveComplete(const Err& err, debug_ipc::RemoveBreakpointReply reply) {
  if (err.has_error()) {
    for (auto& observer : session()->breakpoint_observers())
      observer.OnBreakpointUpdateFailure(this, err);
  }
}

void BreakpointImpl::DidChangeLocation() { SyncBackend(); }

bool BreakpointImpl::CouldApplyToProcess(Process* process) const {
  // When applied to all processes, we need all notifications.
  if (settings_.scope.type() == ExecutionScope::kSystem)
    return true;

  // Target- and thread-specific breakpoints only watch their process.
  return settings_.scope.target() == process->GetTarget();
}

bool BreakpointImpl::HasEnabledLocation() const {
  if (!settings_.enabled)
    return false;
  for (const auto& proc : procs_) {
    if (proc.second.HasEnabledLocation())
      return true;
  }
  return false;
}

bool BreakpointImpl::RegisterProcess(Process* process) {
  // Clear existing locations for this process.
  ProcessRecord& record = procs_[process];
  bool changed = !record.locs.empty();
  record.locs.clear();

  // Resolve addresses.
  ResolveOptions options = GetResolveOptions();
  FindNameContext find_context(process->GetSymbols());

  changed |=
      record.AddLocations(this, process,
                          ResolvePermissiveInputLocations(process->GetSymbols(), options,
                                                          find_context, settings_.locations));
  return changed;
}

ResolveOptions BreakpointImpl::GetResolveOptions() const {
  ResolveOptions options;
  // We don't need the result to be symbolized unless required by skip_function_prologue.

  if (AllLocationsAddresses()) {
    // Only need addresses. Don't try to skip function prologues when the user gives an address or
    // the address might move.
    options.symbolize = false;
    options.skip_function_prologue = false;
  } else {
    // When breaking on symbols or lines, skip function prologues so the function parameters
    // can be displayed properly (they're not always correct in the prologue) as well as backtraces
    // (on ARM, the link register is saved in the prologue so things may look funny before that).
    // Function prologues require symbolization so we ask for both.
    //
    // TODO(bug 45309) we will need an option to control this like other debuggers. LLDB has a
    // per-breakpoint setting and a global default preference. In GDB you can do "break *Foo" to
    // skip the prologue.
    options.symbolize = true;
    options.skip_function_prologue = true;
  }

  return options;
}

bool BreakpointImpl::AllLocationsAddresses() const {
  return !settings_.locations.empty() &&
         std::all_of(settings_.locations.begin(), settings_.locations.end(),
                     [](const auto& loc) { return loc.type == InputLocation::Type::kAddress; });
}

}  // namespace zxdb
