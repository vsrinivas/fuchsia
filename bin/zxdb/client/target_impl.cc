// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/target_impl.h"

#include <sstream>

#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system_impl.h"
#include "garnet/bin/zxdb/client/target_observer.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

TargetImpl::TargetImpl(SystemImpl* system)
    : Target(system->session()),
      weak_thunk_(std::make_shared<WeakThunk<TargetImpl>>(this)) {}
TargetImpl::~TargetImpl() = default;

std::unique_ptr<TargetImpl> TargetImpl::Clone(SystemImpl* system) {
  auto result = std::make_unique<TargetImpl>(system);
  result->args_ = args_;
  return result;
}

Target::State TargetImpl::GetState() const {
  return state_;
}

Process* TargetImpl::GetProcess() const {
  return process_.get();
}

const std::vector<std::string>& TargetImpl::GetArgs() const {
  return args_;
}

void TargetImpl::SetArgs(std::vector<std::string> args) {
  args_ = std::move(args);
}

void TargetImpl::Launch(Callback callback) {
  if (state_ != State::kNone) {
    // TODO(brettw) issue callback asynchronously to avoid reentering caller.
    callback(this, Err("Can't launch, program is already running."));
    return;
  }
  if (args_.empty()) {
    // TODO(brettw) issue callback asynchronously to avoid reentering caller.
    callback(this, Err("No program specified to launch."));
    return;
  }

  debug_ipc::LaunchRequest request;
  request.argv = args_;
  session()->Send<debug_ipc::LaunchRequest, debug_ipc::LaunchReply>(
      request,
      [thunk = std::weak_ptr<WeakThunk<TargetImpl>>(weak_thunk_),
       callback = std::move(callback)](
           const Err& err, debug_ipc::LaunchReply reply) {
        if (auto ptr = thunk.lock()) {
          ptr->thunk->OnLaunchOrAttachReply(err, reply.process_koid,
                                            reply.status, reply.process_name,
                                            std::move(callback));
        } else {
          // The reply that the process was launched came after the local
          // objects were destroyed.
          // TODO(brettw) handle this more gracefully. Maybe kill the remote
          // process?
          fprintf(stderr, "Warning: process launch race, extra process could "
              "be running.\n");
        }
      });
}

void TargetImpl::Attach(uint64_t koid, Callback callback) {
  debug_ipc::AttachRequest request;
  request.koid = koid;
  session()->Send<debug_ipc::AttachRequest, debug_ipc::AttachReply>(
      request,
      [koid, thunk = std::weak_ptr<WeakThunk<TargetImpl>>(weak_thunk_),
       callback](
           const Err& err, debug_ipc::AttachReply reply) {
        if (auto ptr = thunk.lock()) {
          ptr->thunk->OnLaunchOrAttachReply(err, koid, reply.status,
                                            reply.process_name,
                                            std::move(callback));
        } else {
          // The reply that the process was attached came after the local
          // objects were destroyed.
          // TODO(brettw) handle this more gracefully. Maybe kill the remote
          // process?
          fprintf(stderr, "Warning: process attach race, extra process could "
              "be running.\n");
        }
      });
}

void TargetImpl::Detach(Callback callback) {
  if (!process_.get()) {
    // TODO(brettw): Send this asynchronously so as not to surprise callers.
    callback(this, Err("Error detaching: No process."));
    return;
  }

  debug_ipc::DetachRequest request;
  request.process_koid = process_->GetKoid();
  session()->Send<debug_ipc::DetachRequest, debug_ipc::DetachReply>(
      request,
      [thunk = std::weak_ptr<WeakThunk<TargetImpl>>(weak_thunk_),
       callback](
           const Err& err, debug_ipc::DetachReply reply) {
        if (auto ptr = thunk.lock()) {
          ptr->thunk->OnDetachReply(err, reply.status, std::move(callback));
        } else {
          // The reply that the process was launched came after the local
          // objects were destroyed.
          // TODO(brettw) handle this more gracefully. Maybe kill the remote
          // process?
          fprintf(stderr, "Warning: process detach race\n");
        }
      });
}

void TargetImpl::OnProcessExiting(int return_code) {
  FXL_DCHECK(state_ == State::kRunning);

  state_ = State::kNone;
  process_.reset();
  for (auto& observer : observers()) {
    observer.DidDestroyProcess(
        this, TargetObserver::DestroyReason::kExit, return_code);
  }
}

void TargetImpl::OnLaunchOrAttachReply(const Err& err, uint64_t koid,
                                       uint32_t status,
                                       const std::string& process_name,
                                       Callback callback) {
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
    callback(this, issue_err);

  for (auto& observer : observers())
    observer.DidCreateProcess(this, process_.get());
}

void TargetImpl::OnDetachReply(const Err& err, uint32_t status,
                               Callback callback) {
  FXL_DCHECK(process_.get());  // Should have a process.

  Err issue_err;  // Error to send in callback.
  if (err.has_error()) {
    // Error from transport.
    issue_err = err;
  } else if (status != 0) {
    // Error from detaching.
    // TODO(davemoore): Not sure what state the target should be if we error upon detach.
    issue_err = Err(fxl::StringPrintf("Error detaching, status = %d.", status));
  } else {
    // Successfully detached.
    state_ = State::kNone;
    process_.reset();
    for (auto& observer : observers()) {
      observer.DidDestroyProcess(
          this, TargetObserver::DestroyReason::kDetach, 0);
    }
  }

  if (callback)
    callback(this, issue_err);
}

}  // namespace zxdb
