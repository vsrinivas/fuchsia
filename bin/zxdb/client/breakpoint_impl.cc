// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/breakpoint_impl.h"

#include <inttypes.h>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"

namespace zxdb {

namespace {

uint32_t next_breakpoint_id = 1;

Err ValidateSettings(const BreakpointSettings& settings) {
  // Validate scope.
  switch (settings.scope) {
    case BreakpointSettings::Scope::kSystem:
      // TODO(brettw) implement this.
      return Err(
          "System scopes aren't supported since only address breakpoints"
          " are supported now.");
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

BreakpointImpl::BreakpointImpl(Session* session)
    : Breakpoint(session), weak_factory_(this) {}

BreakpointImpl::~BreakpointImpl() {
  if (backend_id_ && settings_.enabled && settings_.scope_target &&
      settings_.scope_target->GetProcess()) {
    // Breakpoint was installed and the process still exists.
    settings_.enabled = false;
    SendBreakpointRemove(settings_.scope_target->GetProcess(),
                         std::function<void(const Err&)>());
  }

  StopObserving();
}

BreakpointSettings BreakpointImpl::GetSettings() const {
  return settings_;
}

void BreakpointImpl::SetSettings(const BreakpointSettings& settings,
                                 std::function<void(const Err&)> callback) {
  Err err = ValidateSettings(settings);
  if (err.has_error()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        [callback, err]() { callback(err); });
    return;
  }

  // It's cleaner to always unregister for existing notifications and
  // re-register for the new ones.
  StopObserving();
  settings_ = settings;
  StartObserving();

  // SendBackendUpdate() will issue the callback in the future.
  err = SendBackendUpdate(callback);
  if (err.has_error()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        [callback, err]() { callback(err); });
  }
}

int BreakpointImpl::GetHitCount() const {
  return hit_count_;
}

void BreakpointImpl::WillDestroyThread(Process* process, Thread* thread) {
  if (settings_.scope_thread == thread) {
    // When the thread is destroyed that the breakpoint is associated with,
    // disable the breakpoint and convert to a target-scoped breakpoint. This
    // will preserve its state without us having to maintain some "defunct
    // thread" association. The user can associate it with a new thread and
    // re-enable as desired.
    StopObserving();
    settings_.scope = BreakpointSettings::Scope::kTarget;
    settings_.scope_target = process->GetTarget();
    settings_.scope_thread = nullptr;
    settings_.enabled = false;
    StartObserving();
  }
}

void BreakpointImpl::DidCreateProcess(Target* target, Process* process) {
  // Register observer for thread changes.
  process->AddObserver(this);
}

void BreakpointImpl::DidDestroyProcess(Target* target, DestroyReason reason,
                                       int exit_code) {
  // When the process exits, disable breakpoints that are address-based since
  // the addresses will normally change when a process is loaded.
  // TODO(brettw) when we have symbolic breakpoints, exclude them from this
  // behavior.
  if (settings_.location_address)
    settings_.enabled = false;
  backend_id_ = 0;
}

void BreakpointImpl::WillDestroyTarget(Target* target) {
  if (target == settings_.scope_target) {
    // As with threads going away, when the target goes away for a
    // target-scoped breakpoint, convert to a disabled system-wide breakpoint.
    StopObserving();
    settings_.scope = BreakpointSettings::Scope::kSystem;
    settings_.scope_target = nullptr;
    settings_.scope_thread = nullptr;
    settings_.enabled = false;
    StartObserving();
  }
}

Err BreakpointImpl::SendBackendUpdate(
    std::function<void(const Err&)> callback) {
  if (!settings_.scope_target)
    return Err("TODO(brettw) need to support non-target breakpoints.");
  Process* process = settings_.scope_target->GetProcess();
  // TODO(brettw) does this need to be deleted???
  if (!process)
    return Err();  // Process not running, don't try to set any breakpoints.

  if (!backend_id_) {
    if (!settings_.enabled) {
      // The breakpoint isn't enabled and there's no backend breakpoint to
      // clear, don't need to do anything.
      debug_ipc::MessageLoop::Current()->PostTask(
          [callback]() { callback(Err()); });
      return Err();
    }

    // Assign a new ID.
    backend_id_ = next_breakpoint_id;
    next_breakpoint_id++;
  } else {
    // Backend breakpoint exists.
    if (!settings_.enabled) {
      // Disable the backend breakpoint.
      SendBreakpointRemove(process, std::move(callback));
      return Err();
    }
  }

  // New or changed breakpoint.
  SendAddOrChange(process, callback);
  return Err();
}

void BreakpointImpl::StopObserving() {
  if (is_system_observer_) {
    session()->system().RemoveObserver(this);
    is_system_observer_ = false;
  }
  if (is_target_observer_) {
    settings_.scope_target->RemoveObserver(this);
    is_target_observer_ = false;
  }
  if (is_process_observer_) {
    // We should be unregistered when the process is going away.
    settings_.scope_target->GetProcess()->RemoveObserver(this);
    is_process_observer_ = false;
  }
}

void BreakpointImpl::StartObserving() {
  // Nothing should be registered (call StopObserving first).
  FXL_DCHECK(!is_system_observer_ && !is_target_observer_ &&
             !is_process_observer_);

  // Always watch the system.
  session()->system().AddObserver(this);
  is_system_observer_ = true;

  if (settings_.scope == BreakpointSettings::Scope::kTarget ||
      settings_.scope == BreakpointSettings::Scope::kThread) {
    settings_.scope_target->AddObserver(this);
    is_target_observer_ = true;
  }
  if (settings_.scope == BreakpointSettings::Scope::kThread) {
    settings_.scope_thread->GetProcess()->AddObserver(this);
    is_process_observer_ = true;
  }
}

void BreakpointImpl::SendAddOrChange(Process* process,
                                     std::function<void(const Err&)> callback) {
  FXL_DCHECK(backend_id_); // ID should have been assigned by the caller.
  FXL_DCHECK(settings_.enabled);  // Shouldn't add or change disabled ones.

  debug_ipc::AddOrChangeBreakpointRequest request;
  request.breakpoint.breakpoint_id = backend_id_;
  request.breakpoint.stop = SettingsStopToIpcStop(settings_.stop_mode);

  request.breakpoint.locations.resize(1);
  request.breakpoint.locations[0].process_koid = process->GetKoid();
  request.breakpoint.locations[0].thread_koid =
      settings_.scope_thread ? settings_.scope_thread->GetKoid() : 0;
  request.breakpoint.locations[0].address = settings_.location_address;

  session()->Send<debug_ipc::AddOrChangeBreakpointRequest,
                  debug_ipc::AddOrChangeBreakpointReply>(
    request,
    [callback, breakpoint = weak_factory_.GetWeakPtr()](
        const Err& err, debug_ipc::AddOrChangeBreakpointReply reply) {
      // Be sure to issue the callback even if the breakpoint no longer exists.
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
        // Backend error. The protocol specifies that errors adding or changing
        // will result in any existing breakpoints with that ID being removed.
        // So mark the breakpoint disabled but keep the settings to the user
        // can fix the problem from the current state if desired.
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

void BreakpointImpl::SendBreakpointRemove(
    Process* process,
    std::function<void(const Err&)> callback) {
  FXL_DCHECK(!settings_.enabled);  // Caller should have disabled already.
  FXL_DCHECK(backend_id_);

  debug_ipc::RemoveBreakpointRequest request;
  request.breakpoint_id = backend_id_;

  session()->Send<debug_ipc::RemoveBreakpointRequest,
                  debug_ipc::RemoveBreakpointReply>(
    request,
    [callback](const Err& err, debug_ipc::RemoveBreakpointReply reply) {
      if (callback)
        callback(Err());
    });

  // Indicate the backend breakpoint is gone.
  backend_id_ = 0;
}

}  // namespace zxdb
