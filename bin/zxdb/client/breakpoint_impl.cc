// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/breakpoint_impl.h"

#include <inttypes.h>
#include <algorithm>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/remote_api.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/symbols/loaded_module_symbols.h"
#include "garnet/bin/zxdb/client/symbols/process_symbols.h"
#include "garnet/bin/zxdb/client/system.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"

namespace zxdb {

namespace {

uint32_t next_breakpoint_id = 1;

Err ValidateSettings(const BreakpointSettings& settings) {
  switch (settings.scope) {
    case BreakpointSettings::Scope::kSystem:
      if (settings.scope_thread || settings.scope_target)
        return Err("System scopes can't take a thread or target.");
      break;
    case BreakpointSettings::Scope::kTarget:
      if (!settings.scope_target)
        return Err(ErrType::kClientApi, "Target scopes require a target.");
      if (settings.scope_thread)
        return Err(ErrType::kClientApi, "Target scopes can't take a thread.");
      break;
    case BreakpointSettings::Scope::kThread:
      if (!settings.scope_target || !settings.scope_thread) {
        return Err(ErrType::kClientApi,
                   "Thread scopes require a target and a thread.");
      }
  }
  return Err();
}

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
  // Set when we're registered as an observer for this process.
  bool observing = false;

  // All resolved locations stored in sorted order.
  std::vector<uint64_t> addresses;
};

BreakpointImpl::BreakpointImpl(Session* session)
    : Breakpoint(session), weak_factory_(this) {
  session->system().AddObserver(this);
}

BreakpointImpl::~BreakpointImpl() {
  if (backend_id_ && settings_.enabled && settings_.scope_target &&
      settings_.scope_target->GetProcess()) {
    // Breakpoint was installed and the process still exists.
    settings_.enabled = false;
    SendBackendRemove(std::function<void(const Err&)>());
  }

  session()->system().RemoveObserver(this);
  for (auto& pair : procs_) {
    if (pair.second.observing) {
      pair.first->RemoveObserver(this);
      pair.second.observing = false;
    }
  }
}

BreakpointSettings BreakpointImpl::GetSettings() const { return settings_; }

void BreakpointImpl::SetSettings(const BreakpointSettings& settings,
                                 std::function<void(const Err&)> callback) {
  Err err = ValidateSettings(settings);
  if (err.has_error()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        [callback, err]() { callback(err); });
    return;
  }

  settings_ = settings;

  for (Target* target : session()->system().GetTargets()) {
    Process* process = target->GetProcess();
    if (process && CouldApplyToProcess(process))
      RegisterProcess(process);
  }

  SyncBackend(std::move(callback));
}

void BreakpointImpl::WillDestroyThread(Process* process, Thread* thread) {
  if (settings_.scope_thread == thread) {
    // When the thread is destroyed that the breakpoint is associated with,
    // disable the breakpoint and convert to a target-scoped breakpoint. This
    // will preserve its state without us having to maintain some "defunct
    // thread" association. The user can associate it with a new thread and
    // re-enable as desired.
    settings_.scope = BreakpointSettings::Scope::kTarget;
    settings_.scope_target = process->GetTarget();
    settings_.scope_thread = nullptr;
    settings_.enabled = false;
  }
}

void BreakpointImpl::DidLoadModuleSymbols(Process* process,
                                          LoadedModuleSymbols* module) {
  // Should only get this notification for relevant processes.
  FXL_DCHECK(CouldApplyToProcess(process));

  // Resolve addresses.
  if (settings_.location_type == BreakpointSettings::LocationType::kSymbol) {
    std::vector<uint64_t> new_addrs =
        module->AddressesForFunction(settings_.location_symbol);
    if (new_addrs.empty())
      return;

    // Merge in the new address(s).
    ProcessRecord& record = procs_[process];
    record.addresses.insert(record.addresses.end(), new_addrs.begin(),
                            new_addrs.end());
    std::sort(record.addresses.begin(), record.addresses.end());
  } else if (settings_.location_type ==
             BreakpointSettings::LocationType::kLine) {
    printf("TODO(brettw) implement line-based breakpoints.\n");
    return;
  }

  // If we get here, something has changed.
  SyncBackend();
}

void BreakpointImpl::WillUnloadModuleSymbols(Process* process,
                                             LoadedModuleSymbols* module) {
  // TODO(brettw) need to get the address range of this module and then
  // remove all breakpoints in that range.
}

void BreakpointImpl::WillDestroyTarget(Target* target) {
  if (target == settings_.scope_target) {
    // By the time the target is destroyed, the process should be gone, and
    // all registrations for breakpoints.
    FXL_DCHECK(!HasLocations());

    // As with threads going away, when the target goes away for a
    // target-scoped breakpoint, convert to a disabled system-wide breakpoint.
    settings_.scope = BreakpointSettings::Scope::kSystem;
    settings_.scope_target = nullptr;
    settings_.scope_thread = nullptr;
    settings_.enabled = false;
  }
}

void BreakpointImpl::GlobalDidCreateProcess(Process* process) {
  if (CouldApplyToProcess(process)) {
    if (RegisterProcess(process))
      SyncBackend();
  }
}

void BreakpointImpl::GlobalWillDestroyProcess(Process* process) {
  auto found = procs_.find(process);
  if (found == procs_.end())
    return;

  if (found->second.observing)
    process->RemoveObserver(this);

  // Only need to update the backend if there was an address associated with
  // this process.
  bool send_update = !found->second.addresses.empty();

  // When the process exits, disable breakpoints that are address-based since
  // the addresses will normally change when a process is loaded.
  if (settings_.location_type == BreakpointSettings::LocationType::kAddress) {
    // Should only have one process for address-based breakpoints.
    FXL_DCHECK(procs_.size() == 1u);
    FXL_DCHECK(process->GetTarget() == settings_.scope_target);
    settings_.enabled = false;
  }

  procs_.erase(found);

  // Needs to be done after the ProcessRecord is removed.
  if (send_update)
    SyncBackend();
}

void BreakpointImpl::SyncBackend(std::function<void(const Err&)> callback) {
  bool has_locations = HasLocations();

  if (backend_id_ && !has_locations) {
    SendBackendRemove(std::move(callback));
  } else if (has_locations) {
    SendBackendAddOrChange(std::move(callback));
  } else {
    // Backend doesn't know about it and we don't require anything.
    if (callback) {
      debug_ipc::MessageLoop::Current()->PostTask(
          [callback]() { callback(Err()); });
    }
  }
}

void BreakpointImpl::SendBackendAddOrChange(
    std::function<void(const Err&)> callback) {
  if (!backend_id_) {
    backend_id_ = next_breakpoint_id;
    next_breakpoint_id++;
  }

  debug_ipc::AddOrChangeBreakpointRequest request;
  request.breakpoint.breakpoint_id = backend_id_;
  request.breakpoint.stop = SettingsStopToIpcStop(settings_.stop_mode);

  for (const auto& proc : procs_) {
    for (uint64_t addr : proc.second.addresses) {
      debug_ipc::ProcessBreakpointSettings addition;
      addition.process_koid = proc.first->GetKoid();

      if (settings_.scope == BreakpointSettings::Scope::kThread)
        addition.thread_koid = settings_.scope_thread->GetKoid();

      addition.address = addr;
      request.breakpoint.locations.push_back(addition);
    }
  }

  if (settings_.scope == BreakpointSettings::Scope::kThread)
    FXL_DCHECK(request.breakpoint.locations.size() == 1u);

  session()->remote_api()->AddOrChangeBreakpoint(request, [
    callback, breakpoint = weak_factory_.GetWeakPtr()
  ](const Err& err, debug_ipc::AddOrChangeBreakpointReply reply) {
    // Be sure to issue the callback even if the breakpoint no longer
    // exists.
    if (err.has_error()) {
      // Transport error. We don't actually know what state the agent is in
      // since it never got the message. In general this means things were
      // disconnected and the agent no longer exists, so mark the breakpoint
      // disabled.
      if (breakpoint)
        breakpoint->settings_.enabled = false;
      if (callback)
        callback(err);
    } else if (reply.status != 0) {
      // Backend error. The protocol specifies that errors adding or
      // changing will result in any existing breakpoints with that ID
      // being removed. So mark the breakpoint disabled but keep the
      // settings to the user can fix the problem from the current state if
      // desired.
      if (breakpoint)
        breakpoint->settings_.enabled = false;
      if (callback)
        callback(Err(ErrType::kGeneral, "Breakpoint set error."));
    } else {
      // Success.
      if (callback)
        callback(Err());
    }
  });
}

void BreakpointImpl::SendBackendRemove(
    std::function<void(const Err&)> callback) {
  FXL_DCHECK(backend_id_);

  debug_ipc::RemoveBreakpointRequest request;
  request.breakpoint_id = backend_id_;

  session()->remote_api()->RemoveBreakpoint(
      request,
      [callback](const Err& err, debug_ipc::RemoveBreakpointReply reply) {
        if (callback)
          callback(err);
      });

  // Indicate the backend breakpoint is gone.
  backend_id_ = 0;
}

bool BreakpointImpl::CouldApplyToProcess(Process* process) const {
  // When applied to all processes, we need all notifications.
  if (settings_.scope == BreakpointSettings::Scope::kSystem)
    return true;

  // Target- and thread-specific breakpoints only watch their process.
  return settings_.scope_target == process->GetTarget();
}

bool BreakpointImpl::HasLocations() const {
  if (!settings_.enabled)
    return false;

  for (const auto& proc : procs_) {
    if (!proc.second.addresses.empty())
      return true;
  }
  return false;
}

bool BreakpointImpl::RegisterProcess(Process* process) {
  bool changed = false;

  if (!procs_[process].observing) {
    procs_[process].observing = true;
    process->AddObserver(this);
  }

  // Resolve addresses.
  ProcessSymbols* symbols = process->GetSymbols();
  if (settings_.location_type == BreakpointSettings::LocationType::kSymbol) {
    std::vector<uint64_t> new_addrs =
        symbols->GetAddressesForFunction(settings_.location_symbol);

    // The ProcessRecord stores sorted addresses (this ensures the comparison
    // below is valid).
    std::sort(new_addrs.begin(), new_addrs.end());

    ProcessRecord& record = procs_[process];
    if (record.addresses != new_addrs) {
      record.addresses = std::move(new_addrs);
      changed = true;
    }
  } else if (settings_.location_type ==
             BreakpointSettings::LocationType::kLine) {
    printf("TODO(brettw) implement line-based breakpoints.\n");
  }
  return changed;
}

}  // namespace zxdb
