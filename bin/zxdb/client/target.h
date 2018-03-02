// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/bin/zxdb/client/weak_thunk.h"
#include "garnet/lib/debug_ipc/protocol.h"

namespace zxdb {

class Err;
class Process;
class System;

// A Target represents the abstract idea of a process that can be debugged.
// This is as opposed to a Process which corresponds to one running process.
//
// Generally upon startup there would be a Target but no Process. This Target
// would receive the breakpoints, process name, command line switches, and
// other state from the user. Running this target would create the associated
// Process object. When the process exits, the Target can be re-used to
// launch the process again with the same configuration.
class Target : public ClientObject {
 public:
  // Callback for Launch(). The integer is the return value from Launch() to
  // correlate the requests.
  using RunCallback = std::function<void(Target*, const Err&)>;

  enum State {
    // The process has not been started or has stopped. From here, it can only
    // transition to starting.
    kStopped,

    // A pending state when the process has been requested to be started but
    // there is no reply from the debug agent yet. From here, it can transition
    // to running (success) or stopped (if launching or attaching failed).
    kStarting,

    // The process is running. From here, it can only transition to stopped.
    kRunning
  };

  // This callback will be called immediately after each state change, so
  // target->state() will represent the new state. In the case of launching,
  // the general callback will be called after the Launch-specific one.
  using StateChangeCallback = std::function<void(Target*, State old_state)>;

  ~Target() override;

  size_t target_id() const { return target_id_; }
  State state() const { return state_; }

  // args[0] is the program name, the rest of the array are the arguments.
  std::vector<std::string>& args() { return args_; }
  const std::vector<std::string>& args() const { return args_; }

  // Returns the process when state() == kRunning. This will be nullptr
  // otherwise.
  Process* process() { return process_.get(); }

  // Returns the transaction ID for this launch. The process must be in
  // kStopped state. args[0] must be set to the program name.
  void Launch(RunCallback callback);

  // Register and unregister for notifications about state changes. The ID
  // returned by the Start() function can be used to unregister with the Stop()
  // function.
  //
  // These observers are global and will apply to all targets.
  static int StartWatchingGlobalStateChanges(StateChangeCallback callback);
  static void StopWatchingGlobalStateChanges(int callback_id);

 private:
  friend class System;

  // The system owns this object and will outlive it. The target_id uniquely
  // identifies this target in the system.
  Target(System* system, size_t target_id);

  // Move, copy, and assign are permitted but only by the (friend) System
  // object which manages the available targets.
  Target(const Target&) = default;
  Target(Target&&) = default;
  Target& operator=(const Target&) = default;
  Target& operator=(Target&&) = default;

  void OnLaunchReply(const Err& err, debug_ipc::LaunchReply reply,
                     RunCallback callback);

  // System that owns this target.
  System* system_;

  size_t target_id_;
  State state_ = kStopped;

  std::vector<std::string> args_;

  // Associated process if there is one.
  std::unique_ptr<Process> process_;

  // Currently registered observers.
  static std::map<int, StateChangeCallback> state_change_callbacks_;
  static int next_state_change_callback_id_;

  std::shared_ptr<WeakThunk<Target>> weak_thunk_;
  // ^ Keep at the bottom to make sure it's destructed last.
};

}  // namespace zxdb
