// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_TARGET_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_TARGET_H_

#include <map>
#include <string>
#include <vector>

#include "lib/fit/function.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/client/client_object.h"
#include "src/developer/debug/zxdb/client/setting_store.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/observer_list.h"

namespace zxdb {

class Err;
class Process;
class System;
class TargetObserver;
class TargetSymbols;

// A Target represents the abstract idea of a process that can be debugged. This is as opposed to a
// Process which corresponds to one running process.
//
// Targets are not initially attached to Processes on the debugged device. There are multiple ways
// of associating a Target with a Process.
//
// If users want to start a new process on the debugged device, they set the breakpoints, process
// name, command line switches, and other state in the Target.  The user then runs the target, which
// creates the associated Process object. When the process exits, the Target can be re-used to
// launch the process again with the same configuration.
//
// If users want to associate a Target with a process on the debugged device, they create a new
// Target (possibly using System::CreateNewTarget), and then call Attach() to perform the
// association.
class Target : public ClientObject {
 public:
  // Note that the callback will be issued in all cases which may be after the target is destroyed.
  // In this case the weak pointer will be null.
  using Callback = fit::callback<void(fxl::WeakPtr<Target> target, const Err&)>;

  enum State {
    // There is no process currently running. From here, it can only transition to starting.
    kNone,

    // A pending state when the process has been requested to be started but there is no reply from
    // the debug agent yet. From here, it can transition to running (success) or stopped (if
    // launching or attaching failed).
    kStarting,

    // A pending state like starting but when we're waiting to attach.
    kAttaching,

    // The process is running. From here, it can only transition to stopped.
    kRunning
  };

  ~Target() override;

  void AddObserver(TargetObserver* observer);
  void RemoveObserver(TargetObserver* observer);

  fxl::WeakPtr<Target> GetWeakPtr();

  // Returns the current process state.
  virtual State GetState() const = 0;

  // Returns the process object if it is currently running (see GetState()). Returns null otherwise.
  virtual Process* GetProcess() const = 0;

  // Returns the process-independent symbol interface. See also Process:GetSymbols().
  virtual const TargetSymbols* GetSymbols() const = 0;

  // Sets and retrieves the arguments passed to the program. args[0] is the program name, the rest
  // of the array are the command-line.
  virtual const std::vector<std::string>& GetArgs() const = 0;
  virtual void SetArgs(std::vector<std::string> args) = 0;

  // Launches the program. The program must be in a kStopped state and the program name configured
  // via SetArgs().
  virtual void Launch(Callback callback) = 0;

  // Kills the process with the given koid. The callback will be executed when the kill is complete
  // (or fails).
  virtual void Kill(Callback callback) = 0;

  // Attaches to the process with the given koid. The callback will be executed when the attach is
  // complete (or fails).
  virtual void Attach(uint64_t koid, Callback callback) = 0;

  // Detaches from the process with the given koid. The callback will be executed when the detach is
  // complete (or fails).
  virtual void Detach(Callback callback) = 0;

  // Notification from the agent that a process has exited.
  virtual void OnProcessExiting(int return_code) = 0;

  // Provides the setting schema for this object.
  static fxl::RefPtr<SettingSchema> GetSchema();

  SettingStore& settings() { return settings_; }

 protected:
  explicit Target(Session* session);

  fxl::ObserverList<TargetObserver>& observers() { return observers_; }

  SettingStore settings_;

 private:
  fxl::ObserverList<TargetObserver> observers_;
  fxl::WeakPtrFactory<Target> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Target);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_TARGET_H_
