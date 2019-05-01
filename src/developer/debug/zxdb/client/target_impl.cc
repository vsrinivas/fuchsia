// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/target_impl.h"

#include <sstream>

#include "src/developer/debug/shared/logging/block_timer.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/client/process_impl.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/system_impl.h"
#include "src/developer/debug/zxdb/client/target_observer.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

TargetImpl::TargetImpl(SystemImpl* system)
    : Target(system->session()),
      system_(system),
      symbols_(system->GetSymbols()),
      impl_weak_factory_(this) {
  settings_.set_fallback(&system_->settings());
  settings_.set_name("target");

  settings_.AddObserver(ClientSettings::Target::kStoreBacktraces, this);
}

TargetImpl::~TargetImpl() {
  // If the process is still running, make sure we broadcast terminated
  // notifications before deleting everything.
  ImplicitlyDetach();
}

std::unique_ptr<TargetImpl> TargetImpl::Clone(SystemImpl* system) {
  auto result = std::make_unique<TargetImpl>(system);
  result->args_ = args_;
  result->symbols_ = symbols_;
  return result;
}

void TargetImpl::ProcessCreatedInJob(uint64_t koid,
                                     const std::string& process_name) {
  FXL_DCHECK(state_ == State::kNone);
  FXL_DCHECK(!process_.get());  // Shouldn't have a process.

  state_ = State::kRunning;
  process_ = CreateProcessImpl(koid, process_name, Process::StartType::kAttach);

  system_->NotifyDidCreateProcess(process_.get());
  for (auto& observer : observers())
    observer.DidCreateProcess(this, process_.get(), true);
}

void TargetImpl::ProcessCreatedAsComponent(uint64_t koid,
                                           const std::string& process_name) {
  FXL_DCHECK(state_ == State::kNone);
  FXL_DCHECK(!process_.get());

  state_ = State::kRunning;
  process_ = CreateProcessImpl(koid, process_name,
                               Process::StartType::kComponent);
  system_->NotifyDidCreateProcess(process_.get());
  for (auto& observer : observers())
    observer.DidCreateProcess(this, process_.get(), false);
}

void TargetImpl::CreateProcessForTesting(uint64_t koid,
                                         const std::string& process_name) {
  FXL_DCHECK(state_ == State::kNone);
  state_ = State::kStarting;
  OnLaunchOrAttachReply(Callback(), Err(), koid, 0, process_name);
}

void TargetImpl::ImplicitlyDetach() {
  if (GetProcess()) {
    OnKillOrDetachReply(TargetObserver::DestroyReason::kDetach, Err(), 0,
                        [](fxl::WeakPtr<Target>, const Err&) {});
  }
}

Target::State TargetImpl::GetState() const { return state_; }

Process* TargetImpl::GetProcess() const { return process_.get(); }

const TargetSymbols* TargetImpl::GetSymbols() const { return &symbols_; }

const std::vector<std::string>& TargetImpl::GetArgs() const { return args_; }

void TargetImpl::SetArgs(std::vector<std::string> args) {
  args_ = std::move(args);
}

void TargetImpl::Launch(Callback callback) {
  Err err;
  if (state_ != State::kNone)
    err = Err("Can't launch, program is already running or starting.");
  else if (args_.empty())
    err = Err("No program specified to launch.");

  if (err.has_error()) {
    // Avoid reentering caller to dispatch the error.
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [callback, err, weak_ptr = GetWeakPtr()]() {
          callback(std::move(weak_ptr), err);
        });
    return;
  }

  state_ = State::kStarting;

  debug_ipc::LaunchRequest request;
  request.inferior_type = debug_ipc::InferiorType::kBinary;
  request.argv = args_;
  session()->remote_api()->Launch(
      request, [callback, weak_target = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::LaunchReply reply) {
        TargetImpl::OnLaunchOrAttachReplyThunk(weak_target, callback, err,
                                               reply.process_id, reply.status,
                                               reply.process_name);
      });
}

void TargetImpl::Kill(Callback callback) {
  if (!process_.get()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [callback, weak_ptr = GetWeakPtr()]() {
          callback(std::move(weak_ptr), Err("Error killing: No process."));
        });
    return;
  }

  debug_ipc::KillRequest request;
  request.process_koid = process_->GetKoid();
  session()->remote_api()->Kill(
      request, [callback, weak_target = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::KillReply reply) {
        if (weak_target) {
          weak_target->OnKillOrDetachReply(TargetObserver::DestroyReason::kKill,
                                           err, reply.status,
                                           std::move(callback));
        } else {
          // The reply that the process was launched came after the local
          // objects were destroyed. We're still OK to dispatch either way.
          callback(weak_target, err);
        }
      });
}

void TargetImpl::Attach(uint64_t koid, Callback callback) {
  if (state_ != State::kNone) {
    // Avoid reentering caller to dispatch the error.
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [callback, weak_ptr = GetWeakPtr()]() {
          callback(
              std::move(weak_ptr),
              Err("Can't attach, program is already running or starting."));
        });
    return;
  }

  state_ = State::kAttaching;

  debug_ipc::AttachRequest request;
  request.koid = koid;
  session()->remote_api()->Attach(
      request, [koid, callback, weak_target = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::AttachReply reply) {
        OnLaunchOrAttachReplyThunk(std::move(weak_target), std::move(callback),
                                   err, koid, reply.status, reply.name);
      });
}

void TargetImpl::Detach(Callback callback) {
  if (!process_.get()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [callback, weak_ptr = GetWeakPtr()]() {
          callback(std::move(weak_ptr), Err("Error detaching: No process."));
        });
    return;
  }

  debug_ipc::DetachRequest request;
  request.koid = process_->GetKoid();
  session()->remote_api()->Detach(
      request, [callback, weak_target = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::DetachReply reply) {
        if (weak_target) {
          weak_target->OnKillOrDetachReply(
              TargetObserver::DestroyReason::kDetach, err, reply.status,
              std::move(callback));
        } else {
          // The reply that the process was launched came after the local
          // objects were destroyed. We're still OK to dispatch either way.
          callback(weak_target, err);
        }
      });
}

void TargetImpl::OnProcessExiting(int return_code) {
  FXL_DCHECK(state_ == State::kRunning);
  state_ = State::kNone;

  system_->NotifyWillDestroyProcess(process_.get());
  for (auto& observer : observers()) {
    observer.WillDestroyProcess(this, process_.get(),
                                TargetObserver::DestroyReason::kExit,
                                return_code);
  }

  process_.reset();
}

// static
void TargetImpl::OnLaunchOrAttachReplyThunk(fxl::WeakPtr<TargetImpl> target,
                                            Callback callback, const Err& err,
                                            uint64_t koid,
                                            debug_ipc::zx_status_t status,
                                            const std::string& process_name) {
  if (target) {
    target->OnLaunchOrAttachReply(std::move(callback), err, koid, status,
                                  process_name);
  } else {
    // The reply that the process was launched came after the local
    // objects were destroyed.
    if (err.has_error()) {
      // Process not launched, forward the error.
      callback(target, err);
    } else {
      // TODO(brettw) handle this more gracefully. Maybe kill the remote
      // process?
      callback(target, Err("Warning: process launch race, extra process is "
                           "likely running."));
    }
  }
}

void TargetImpl::OnSettingChanged(const SettingStore& store,
                                  const std::string& setting_name) {
  if (setting_name == ClientSettings::Target::kStoreBacktraces) {
    bool should_store = store.GetBool(setting_name);
    if (!process_)
      return;
    process_->ShouldStoreBacktraces(should_store);
  } else {
    FXL_LOG(WARNING) << "Target setting unhandled: " << setting_name;
  }
}

void TargetImpl::OnLaunchOrAttachReply(Callback callback, const Err& err,
                                       uint64_t koid,
                                       debug_ipc::zx_status_t status,
                                       const std::string& process_name) {
  FXL_DCHECK(state_ == State::kAttaching || state_ == State::kStarting);
  FXL_DCHECK(!process_.get());  // Shouldn't have a process.

  Err issue_err;  // Error to send in callback.
  if (err.has_error()) {
    // Error from transport.
    state_ = State::kNone;
    issue_err = err;
  } else if (status != 0) {
    // Error from launching.
    state_ = State::kNone;
    if (status == debug_ipc::kZxErrIO) {
      issue_err = Err("Error launching: Binary not found [%s]",
                      debug_ipc::ZxStatusToString(status));
    } else {
      issue_err = Err(fxl::StringPrintf("Error launching, status = %s.",
                                        debug_ipc::ZxStatusToString(status)));
    }
  } else {
    Process::StartType start_type = state_ == State::kAttaching
                                        ? Process::StartType::kAttach
                                        : Process::StartType::kLaunch;
    state_ = State::kRunning;
    process_ = CreateProcessImpl(koid, process_name, start_type);
  }

  if (callback)
    callback(GetWeakPtr(), issue_err);

  if (state_ == State::kRunning) {
    system_->NotifyDidCreateProcess(process_.get());
    for (auto& observer : observers())
      observer.DidCreateProcess(this, process_.get(), false);
  }
}

void TargetImpl::OnKillOrDetachReply(TargetObserver::DestroyReason reason,
                                     const Err& err, int32_t status,
                                     Callback callback) {
  FXL_DCHECK(process_.get());  // Should have a process.

  Err issue_err;  // Error to send in callback.
  if (err.has_error()) {
    // Error from transport.
    state_ = State::kNone;
    issue_err = err;
  } else if (status != 0) {
    // Error from detaching.
    // TODO(davemoore): Not sure what state the target should be if we error
    // upon detach.
    issue_err =
        Err(fxl::StringPrintf("Error %sing, status = %s.",
                              TargetObserver::DestroyReasonToString(reason),
                              debug_ipc::ZxStatusToString(status)));
  } else {
    // Successfully detached.
    state_ = State::kNone;
    system_->NotifyWillDestroyProcess(process_.get());

    // Keep the process alive for the observer call, but remove it from the
    // target as per the observer specification.
    std::unique_ptr<ProcessImpl> doomed_process = std::move(process_);
    for (auto& observer : observers())
      observer.WillDestroyProcess(this, doomed_process.get(), reason, 0);
  }

  callback(GetWeakPtr(), issue_err);
}

std::unique_ptr<ProcessImpl> TargetImpl::CreateProcessImpl(
    uint64_t koid, const std::string& name, Process::StartType start_type) {
  auto process = std::make_unique<ProcessImpl>(this, koid, name,
                                               Process::StartType::kAttach);
  if (settings_.GetBool(ClientSettings::Target::kStoreBacktraces))
    process->ShouldStoreBacktraces(true);
  return process;
}

}  // namespace zxdb
