// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/target.h"

#include <sstream>

#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

// static
std::map<int, Target::StateChangeCallback> Target::state_change_callbacks_;
int Target::next_state_change_callback_id_ = 1;

Target::Target(System* system, size_t target_id)
    : ClientObject(system->session()),
      system_(system),
      target_id_(target_id),
      weak_thunk_(std::make_shared<WeakThunk<Target>>(this)) {}
Target::~Target() {}

void Target::Launch(RunCallback callback) {
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
      [thunk = std::weak_ptr<WeakThunk<Target>>(weak_thunk_),
       callback = std::move(callback)](
           Session*, uint32_t transaction_id, const Err& err,
           debug_ipc::LaunchReply reply) {
        if (auto ptr = thunk.lock()) {
          ptr->thunk->OnLaunchReply(err, std::move(reply),
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

// static
int Target::StartWatchingGlobalStateChanges(StateChangeCallback callback) {
  int id = next_state_change_callback_id_;
  next_state_change_callback_id_++;
  state_change_callbacks_[id] = callback;
  return id;
}

// static
void Target::StopWatchingGlobalStateChanges(int callback_id) {
  auto found = state_change_callbacks_.find(callback_id);
  if (found == state_change_callbacks_.end())
    FXL_NOTREACHED();
  else
    state_change_callbacks_.erase(found);
}

void Target::OnLaunchReply(const Err& err, debug_ipc::LaunchReply reply,
                           RunCallback callback) {
  FXL_DCHECK(state_ = State::kStarting);
  FXL_DCHECK(!process_.get());  // Shouldn't have a process.
  if (err.has_error()) {
    // Error from transport.
    state_ = State::kStopped;
    callback(this, err);
  } else if (reply.status != 0) {
    // Error from launching.
    state_ = State::kStopped;
    callback(this, Err(fxl::StringPrintf(
            "Error launching, status = %d.", reply.status)));
  } else {
    state_ = State::kRunning;
    process_ = std::make_unique<Process>(this, reply.process_koid);
    callback(this, Err());
  }

  for (const auto& cb : state_change_callbacks_)
    cb.second(this, State::kStarting);
}

}  // namespace zxdb
