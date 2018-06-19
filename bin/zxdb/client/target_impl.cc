// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/target_impl.h"

#include <sstream>

#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/remote_api.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system_impl.h"
#include "garnet/bin/zxdb/client/target_observer.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

TargetImpl::TargetImpl(SystemImpl* system)
    : Target(system->session()),
      system_(system),
      symbols_(system->GetSymbols()),
      impl_weak_factory_(this) {}
TargetImpl::~TargetImpl() = default;

std::unique_ptr<TargetImpl> TargetImpl::Clone(SystemImpl* system) {
  auto result = std::make_unique<TargetImpl>(system);
  result->args_ = args_;
  result->symbols_ = symbols_;
  return result;
}

void TargetImpl::CreateProcessForTesting(uint64_t koid,
                                         const std::string& process_name) {
  FXL_DCHECK(state_ == State::kNone);
  state_ = State::kStarting;
  OnLaunchOrAttachReply(Callback(), Err(), koid, 0, process_name);
}

void TargetImpl::DestroyProcessForTesting() {
  OnKillOrDetachReply(Err(), 0, Callback());
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
    err = Err("Can't launch, program is already running.");
  else if (args_.empty())
    err = Err("No program specified to launch.");

  if (err.has_error()) {
    // Avoid reentering caller to dispatch the error.
    debug_ipc::MessageLoop::Current()->PostTask(
        [callback, err, weak_ptr = GetWeakPtr()]() {
          callback(std::move(weak_ptr), err);
        });
    return;
  }

  debug_ipc::LaunchRequest request;
  request.argv = args_;
  session()->remote_api()->Launch(
      request, [callback, weak_target = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::LaunchReply reply) {
        TargetImpl::OnLaunchOrAttachReplyThunk(weak_target, callback, err,
                                               reply.process_koid, reply.status,
                                               reply.process_name);
      });
}

void TargetImpl::Kill(Callback callback) {
  if (!process_.get()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        [callback, weak_ptr = GetWeakPtr()]() {
          callback(std::move(weak_ptr), Err("Error detaching: No process."));
        });
    return;
  }

  debug_ipc::KillRequest request;
  request.process_koid = process_->GetKoid();
  session()->remote_api()->Kill(
      request, [callback, weak_target = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::KillReply reply) {
        if (weak_target) {
          weak_target->OnKillOrDetachReply(err, reply.status,
                                           std::move(callback));
        } else {
          // The reply that the process was launched came after the local
          // objects were destroyed. We're still OK to dispatch either way.
          callback(weak_target, err);
        }
      });
}

void TargetImpl::Attach(uint64_t koid, Callback callback) {
  debug_ipc::AttachRequest request;
  request.koid = koid;
  session()->remote_api()->Attach(
      request, [koid, callback, weak_target = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::AttachReply reply) {
        OnLaunchOrAttachReplyThunk(std::move(weak_target), std::move(callback),
                                   err, koid, reply.status, reply.process_name);
      });
}

void TargetImpl::Detach(Callback callback) {
  if (!process_.get()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        [callback, weak_ptr = GetWeakPtr()]() {
          callback(std::move(weak_ptr), Err("Error detaching: No process."));
        });
    return;
  }

  debug_ipc::DetachRequest request;
  request.process_koid = process_->GetKoid();
  session()->remote_api()->Detach(
      request, [callback, weak_target = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::DetachReply reply) {
        if (weak_target) {
          weak_target->OnKillOrDetachReply(err, reply.status,
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
                                            uint64_t koid, uint32_t status,
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

void TargetImpl::OnLaunchOrAttachReply(Callback callback, const Err& err,
                                       uint64_t koid, uint32_t status,
                                       const std::string& process_name) {
  FXL_DCHECK(state_ = State::kStarting);
  FXL_DCHECK(!process_.get());  // Shouldn't have a process.

  Err issue_err;  // Error to send in callback.
  if (err.has_error()) {
    // Error from transport.
    issue_err = err;
  } else if (status != 0) {
    // Error from launching.
    state_ = State::kNone;
    issue_err = Err(fxl::StringPrintf("Error launching, status = %d.", status));
  } else {
    state_ = State::kRunning;
    process_ = std::make_unique<ProcessImpl>(this, koid, process_name);
  }

  if (callback)
    callback(GetWeakPtr(), issue_err);

  if (state_ == State::kRunning) {
    system_->NotifyDidCreateProcess(process_.get());
    for (auto& observer : observers())
      observer.DidCreateProcess(this, process_.get());
  }
}

void TargetImpl::OnKillOrDetachReply(const Err& err, uint32_t status,
                                     Callback callback) {
  FXL_DCHECK(process_.get());  // Should have a process.

  Err issue_err;  // Error to send in callback.
  if (err.has_error()) {
    // Error from transport.
    issue_err = err;
  } else if (status != 0) {
    // Error from detaching.
    // TODO(davemoore): Not sure what state the target should be if we error
    // upon detach.
    issue_err = Err(fxl::StringPrintf("Error detaching, status = %d.", status));
  } else {
    // Successfully detached.
    state_ = State::kNone;
    system_->NotifyWillDestroyProcess(process_.get());

    // Keep the process alive for the observer call, but remove it from the
    // target as per the observer specification.
    std::unique_ptr<ProcessImpl> doomed_process = std::move(process_);
    for (auto& observer : observers()) {
      observer.WillDestroyProcess(this, doomed_process.get(),
                                  TargetObserver::DestroyReason::kDetach, 0);
    }
  }

  if (callback)
    callback(GetWeakPtr(), issue_err);
}

}  // namespace zxdb
