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

int64_t TargetImpl::GetLastReturnCode() const {
  return last_return_code_;
}

void TargetImpl::Launch(LaunchCallback callback) {
  if (state_ != State::kStopped) {
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
           Session*, uint32_t transaction_id, const Err& err,
           debug_ipc::LaunchReply reply) {
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

  for (auto& observer : observers())
    observer.DidChangeTargetState(this, State::kStopped);
}

void TargetImpl::Attach(uint64_t koid, LaunchCallback callback) {
  debug_ipc::AttachRequest request;
  request.koid = koid;
  session()->Send<debug_ipc::AttachRequest, debug_ipc::AttachReply>(
      request,
      [koid, thunk = std::weak_ptr<WeakThunk<TargetImpl>>(weak_thunk_),
       callback = std::move(callback)](
           Session*, uint32_t transaction_id, const Err& err,
           debug_ipc::AttachReply reply) {
        if (auto ptr = thunk.lock()) {
          ptr->thunk->OnLaunchOrAttachReply(err, koid, reply.status,
                                            reply.process_name,
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

void TargetImpl::OnProcessExiting(int return_code) {
  FXL_DCHECK(state_ == State::kRunning);

  last_return_code_ = return_code;
  state_ = State::kStopped;
  process_.reset();

  for (auto& observer : observers())
    observer.DidChangeTargetState(this, State::kRunning);
}

void TargetImpl::OnLaunchOrAttachReply(const Err& err, uint64_t koid,
                                       uint32_t status,
                                       const std::string& process_name,
                                       LaunchCallback callback) {
  FXL_DCHECK(state_ = State::kStarting);
  FXL_DCHECK(!process_.get());  // Shouldn't have a process.

  Err issue_err;  // Error to send in callback.
  if (err.has_error()) {
    // Error from transport.
    issue_err = err;
  } else if (status != 0) {
    // Error from launching.
    state_ = State::kStopped;
    issue_err = Err(fxl::StringPrintf("Error launching, status = %d.", status));
  } else {
    state_ = State::kRunning;
    process_ = std::make_unique<ProcessImpl>(this, koid, process_name);
  }

  if (callback)
    callback(this, issue_err);

  for (auto& observer : observers())
    observer.DidChangeTargetState(this, State::kStarting);
}

}  // namespace zxdb
