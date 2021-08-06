// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/target_impl.h"

#include <lib/syslog/cpp/macros.h>

#include <sstream>

#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/status.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/client/process_impl.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

TargetImpl::TargetImpl(System* system)
    : Target(system->session()),
      system_(system),
      symbols_(system->GetSymbols()),
      impl_weak_factory_(this) {
  settings_.set_fallback(&system_->settings());
}

TargetImpl::~TargetImpl() {
  // If the process is still running, make sure we broadcast terminated notifications before
  // deleting everything.
  ImplicitlyDetach();
}

std::unique_ptr<TargetImpl> TargetImpl::Clone(System* system) {
  auto result = std::make_unique<TargetImpl>(system);
  result->args_ = args_;
  result->symbols_ = symbols_;
  return result;
}

void TargetImpl::ProcessCreatedInJob(uint64_t koid, const std::string& process_name,
                                     uint64_t timestamp) {
  FX_DCHECK(state_ == State::kNone);
  FX_DCHECK(!process_.get());  // Shouldn't have a process.

  state_ = State::kRunning;
  process_ = CreateProcessImpl(koid, process_name, Process::StartType::kAttach);

  for (auto& observer : session()->process_observers())
    observer.DidCreateProcess(process_.get(), true, timestamp);
}

void TargetImpl::ProcessCreatedAsComponent(uint64_t koid, const std::string& process_name,
                                           uint64_t timestamp) {
  FX_DCHECK(state_ == State::kNone);
  FX_DCHECK(!process_.get());

  state_ = State::kRunning;
  process_ = CreateProcessImpl(koid, process_name, Process::StartType::kComponent);

  for (auto& observer : session()->process_observers())
    observer.DidCreateProcess(process_.get(), false, timestamp);
}

void TargetImpl::CreateProcessForTesting(uint64_t koid, const std::string& process_name) {
  FX_DCHECK(state_ == State::kNone);
  state_ = State::kStarting;
  uint64_t cur_mock_timestamp = mock_timestamp_;
  mock_timestamp_ += 1000;
  OnLaunchOrAttachReply(CallbackWithTimestamp(), Err(), koid, process_name, cur_mock_timestamp);
}

void TargetImpl::ImplicitlyDetach() {
  if (GetProcess()) {
    OnKillOrDetachReply(
        ProcessObserver::DestroyReason::kDetach, Err(), debug::Status(),
        [](fxl::WeakPtr<Target>, const Err&) {}, 0);
  }
}

Target::State TargetImpl::GetState() const { return state_; }

Process* TargetImpl::GetProcess() const { return process_.get(); }

const TargetSymbols* TargetImpl::GetSymbols() const { return &symbols_; }

const std::vector<std::string>& TargetImpl::GetArgs() const { return args_; }

void TargetImpl::SetArgs(std::vector<std::string> args) { args_ = std::move(args); }

void TargetImpl::Launch(CallbackWithTimestamp callback) {
  Err err;
  if (state_ != State::kNone)
    err = Err("Can't launch, program is already running or starting.");
  else if (args_.empty())
    err = Err("No program specified to launch.");

  if (err.has_error()) {
    // Avoid reentering caller to dispatch the error.
    debug::MessageLoop::Current()->PostTask(
        FROM_HERE, [callback = std::move(callback), err, weak_ptr = GetWeakPtr()]() mutable {
          callback(std::move(weak_ptr), err, 0);
        });
    return;
  }

  state_ = State::kStarting;

  debug_ipc::LaunchRequest request;
  request.inferior_type = debug_ipc::InferiorType::kBinary;
  request.argv = args_;
  session()->remote_api()->Launch(
      request, [callback = std::move(callback), weak_target = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::LaunchReply reply) mutable {
        TargetImpl::OnLaunchOrAttachReplyThunk(weak_target, std::move(callback), err,
                                               reply.process_id, reply.status, reply.process_name,
                                               reply.timestamp);
      });
}

void TargetImpl::Kill(Callback callback) {
  if (!process_.get()) {
    debug::MessageLoop::Current()->PostTask(
        FROM_HERE, [callback = std::move(callback), weak_ptr = GetWeakPtr()]() mutable {
          callback(std::move(weak_ptr), Err("Error killing: No process."));
        });
    return;
  }

  debug_ipc::KillRequest request;
  request.process_koid = process_->GetKoid();
  session()->remote_api()->Kill(
      request, [callback = std::move(callback), weak_target = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::KillReply reply) mutable {
        if (weak_target) {
          weak_target->OnKillOrDetachReply(ProcessObserver::DestroyReason::kKill, err, reply.status,
                                           std::move(callback), reply.timestamp);
        } else {
          // The reply that the process was launched came after the local objects were destroyed.
          // We're still OK to dispatch either way.
          callback(weak_target, err);
        }
      });
}

void TargetImpl::Attach(uint64_t koid, CallbackWithTimestamp callback) {
  if (state_ != State::kNone) {
    // Avoid reentering caller to dispatch the error.
    debug::MessageLoop::Current()->PostTask(
        FROM_HERE, [callback = std::move(callback), weak_ptr = GetWeakPtr()]() mutable {
          callback(std::move(weak_ptr),
                   Err("Can't attach, program is already running or starting."), 0);
        });
    return;
  }

  state_ = State::kAttaching;

  debug_ipc::AttachRequest request;
  request.koid = koid;
  session()->remote_api()->Attach(
      request,
      [koid, callback = std::move(callback), weak_target = impl_weak_factory_.GetWeakPtr()](
          const Err& err, debug_ipc::AttachReply reply) mutable {
        OnLaunchOrAttachReplyThunk(std::move(weak_target), std::move(callback), err, koid,
                                   reply.status, reply.name, reply.timestamp);
      });
}

void TargetImpl::Detach(Callback callback) {
  if (!process_.get()) {
    debug::MessageLoop::Current()->PostTask(
        FROM_HERE, [callback = std::move(callback), weak_ptr = GetWeakPtr()]() mutable {
          callback(std::move(weak_ptr), Err("Error detaching: No process."));
        });
    return;
  }

  debug_ipc::DetachRequest request;
  request.koid = process_->GetKoid();
  session()->remote_api()->Detach(
      request, [callback = std::move(callback), weak_target = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::DetachReply reply) mutable {
        if (weak_target) {
          weak_target->OnKillOrDetachReply(ProcessObserver::DestroyReason::kDetach, err,
                                           reply.status, std::move(callback), reply.timestamp);
        } else {
          // The reply that the process was launched came after the local objects were destroyed.
          // We're still OK to dispatch either way.
          callback(weak_target, err);
        }
      });
}

void TargetImpl::OnProcessExiting(int return_code, uint64_t timestamp) {
  FX_DCHECK(state_ == State::kRunning);
  state_ = State::kNone;

  for (auto& observer : session()->process_observers())
    observer.WillDestroyProcess(process_.get(), ProcessObserver::DestroyReason::kExit, return_code,
                                timestamp);

  process_.reset();
}

// static
void TargetImpl::OnLaunchOrAttachReplyThunk(fxl::WeakPtr<TargetImpl> target,
                                            CallbackWithTimestamp callback, const Err& input_err,
                                            uint64_t koid, const debug::Status& status,
                                            const std::string& process_name, uint64_t timestamp) {
  // The input Err indicates a transport error while the debug::Status indicates the remote error,
  // map them to a single value.
  Err err = input_err;
  if (err.ok())
    err = Err(status);

  if (target) {
    target->OnLaunchOrAttachReply(std::move(callback), err, koid, process_name, timestamp);
  } else {
    // The reply that the process was launched came after the local objects were destroyed.
    if (err.has_error()) {
      // Process not launched, forward the error.
      callback(target, err, timestamp);
    } else {
      // TODO(brettw) handle this more gracefully. Maybe kill the remote process?
      callback(target, Err("Warning: process launch race, extra process is likely running."),
               timestamp);
    }
  }
}

void TargetImpl::OnLaunchOrAttachReply(CallbackWithTimestamp callback, const Err& err,
                                       uint64_t koid, const std::string& process_name,
                                       uint64_t timestamp) {
  FX_DCHECK(state_ == State::kAttaching || state_ == State::kStarting);
  FX_DCHECK(!process_.get());  // Shouldn't have a process.

  if (err.has_error()) {
    if (err.type() == ErrType::kAlreadyExists) {
      HandleAttachAlreadyExists(std::move(callback), koid, process_name, timestamp);
      return;  // The above handler will issue the callback.
    }
    state_ = State::kNone;
  } else {
    Process::StartType start_type =
        state_ == State::kAttaching ? Process::StartType::kAttach : Process::StartType::kLaunch;
    state_ = State::kRunning;
    process_ = CreateProcessImpl(koid, process_name, start_type);
  }

  if (callback)
    callback(GetWeakPtr(), err, timestamp);

  if (state_ == State::kRunning) {
    for (auto& observer : session()->process_observers())
      observer.DidCreateProcess(process_.get(), false, timestamp);
  }
}

void TargetImpl::HandleAttachAlreadyExists(CallbackWithTimestamp callback, uint64_t koid,
                                           const std::string& process_name, uint64_t timestamp) {
  // "Already exists" can mean two things. In the first case, it could be a mistake where the user
  // is trying to attach to the same process twice.
  if (system_->ProcessFromKoid(koid)) {
    callback(GetWeakPtr(), Err("Process " + std::to_string(koid) + " is already being debugged."),
             timestamp);
    return;
  }

  // The other case for "already exists" is that the agent is attached to that process already
  // but the debugger doesn't know about it. This can happen when connecting to an already-running
  // debug agent. In this case, get the status to sync with the agent.
  debug_ipc::ProcessStatusRequest request = {};
  request.process_koid = koid;

  DEBUG_LOG(Session) << "Re-attaching to process " << process_name << " (" << koid << ").";
  session()->remote_api()->ProcessStatus(
      request, [target = GetWeakPtr(), callback = std::move(callback), process_name, timestamp](
                   const Err& err, debug_ipc::ProcessStatusReply reply) mutable {
        if (!target)
          return;

        if (err.has_error())
          return callback(target, err, timestamp);

        if (reply.status.has_error()) {
          Err error("Could not attach to process %s: %s", process_name.c_str(),
                    reply.status.message().c_str());
          return callback(target, err, timestamp);
        }

        return callback(target, Err(), timestamp);
      });
}

void TargetImpl::OnKillOrDetachReply(ProcessObserver::DestroyReason reason, const Err& err,
                                     const debug::Status& status, Callback callback,
                                     uint64_t timestamp) {
  FX_DCHECK(process_.get());  // Should have a process.

  Err issue_err;  // Error to send in callback.
  if (err.has_error()) {
    // Error from transport.
    state_ = State::kNone;
    issue_err = err;
  } else if (status.has_error()) {
    // Error from detaching.
    // TODO(davemoore): Not sure what state the target should be if we error upon detach.
    issue_err =
        Err(fxl::StringPrintf("Error %sing: %s", ProcessObserver::DestroyReasonToString(reason),
                              status.message().c_str()));
  } else {
    // Successfully detached.
    state_ = State::kNone;

    // Keep the process alive for the observer call, but remove it from the target as per the
    // observer specification.
    for (auto& observer : session()->process_observers())
      observer.WillDestroyProcess(process_.get(), reason, 0, timestamp);

    process_.reset();
  }

  callback(GetWeakPtr(), issue_err);
}

std::unique_ptr<ProcessImpl> TargetImpl::CreateProcessImpl(uint64_t koid, const std::string& name,
                                                           Process::StartType start_type) {
  return std::make_unique<ProcessImpl>(this, koid, name, Process::StartType::kAttach);
}

}  // namespace zxdb
