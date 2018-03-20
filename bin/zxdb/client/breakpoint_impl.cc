// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/breakpoint_impl.h"

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/client/thread.h"

namespace zxdb {

BreakpointImpl::BreakpointImpl(Session* session) : Breakpoint(session) {}

BreakpointImpl::~BreakpointImpl() {
  StartObserving();
}

bool BreakpointImpl::IsEnabled() const {
  return enabled_;
}

Err BreakpointImpl::SetEnabled(bool enabled) {
  enabled_ = enabled;
  return Err();
}

Err BreakpointImpl::SetScope(Scope scope, Target* target, Thread* thread) {
  // Validate input.
  switch (scope) {
    case Scope::kSystem:
      // TODO(brettw) implement this.
      return Err(
          "System scopes aren't supported since only address breakpoints"
          " are supported now.");
    case Scope::kTarget:
      if (!target)
        return Err(ErrType::kClientApi, "Target scopes require a target.");
      if (thread)
        return Err(ErrType::kClientApi, "Target scopes can't take a thread.");
      break;
    case Scope::kThread:
      if (!target || !thread) {
        return Err(ErrType::kClientApi,
                   "Thread scopes require a target and a thread.");
      }
  }

  // It's cleaner to always unregister for existing notifications and
  // re-register for the new ones.
  StopObserving();

  // Don't return early between here and StartObserving() or we may miss
  // a notification.
  scope_ = scope;
  target_scope_ = target;
  thread_scope_ = thread;

  StartObserving();
  return Err();
}

Breakpoint::Scope BreakpointImpl::GetScope(Target** target,
                                           Thread** thread) const {
  *target = target_scope_;
  *thread = thread_scope_;
  return scope_;
}

void BreakpointImpl::SetStopMode(Stop stop) {
  stop_mode_ = stop;
}

Breakpoint::Stop BreakpointImpl::GetStopMode() const {
  return stop_mode_;
}

void BreakpointImpl::SetAddressLocation(uint64_t address) {
  address_ = address;
}

uint64_t BreakpointImpl::GetAddressLocation() const {
  return address_;
}

int BreakpointImpl::GetHitCount() const {
  return hit_count_;
}

void BreakpointImpl::WillDestroyThread(Process* process, Thread* thread) {
  if (thread_scope_ == thread) {
    // When the thread is destroyed that the breakpoint is associated with,
    // disable the breakpoint and convert to a target-scoped breakpoint. This
    // will preserve its state without us having to maintain some "defunct
    // thread" association. The user can associate it with a new thread and
    // re-enable as desired.
    StopObserving();
    scope_ = Scope::kTarget;
    target_scope_ = process->GetTarget();
    thread_scope_ = nullptr;
    enabled_ = false;
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
  if (address_) {
    enabled_ = false;
  }
}

void BreakpointImpl::WillDestroyTarget(Target* target) {
  if (target == target_scope_) {
    // As with threads going away, when the target goes away for a
    // target-scoped breakpoint, convert to a disabled system-wide breakpoint.
    StopObserving();
    scope_ = Scope::kSystem;
    target_scope_ = nullptr;
    thread_scope_ = nullptr;
    enabled_ = false;
    StartObserving();
  }
}

void BreakpointImpl::StopObserving() {
  if (is_system_observer_) {
    session()->system().RemoveObserver(this);
    is_system_observer_ = false;
  }

  if (is_target_observer_) {
    target_scope_->RemoveObserver(this);
    is_target_observer_ = false;
  }
  if (is_process_observer_) {
    // We should be unregistered when the process is going away.
    target_scope_->GetProcess()->RemoveObserver(this);
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

  if (scope_ == Scope::kTarget || scope_ == Scope::kThread) {
    target_scope_->AddObserver(this);
    is_target_observer_ = true;
  }
  if (scope_ == Scope::kThread) {
    thread_scope_->GetProcess()->AddObserver(this);
    is_process_observer_ = true;
  }
}

}  // namespace zxdb
