// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <vector>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/public/lib/fxl/macros.h"
#include "garnet/public/lib/fxl/observer_list.h"

namespace zxdb {

class Breakpoint;
class Err;
class SystemObserver;
class SystemSymbols;

// Represents system-wide state on the debugged computer.
class System : public ClientObject {
 public:
  // Callback for requesting the process tree.
  using ProcessTreeCallback =
      std::function<void(const Err&, debug_ipc::ProcessTreeReply)>;

  System(Session* session);
  ~System() override;

  void AddObserver(SystemObserver* observer);
  void RemoveObserver(SystemObserver* observer);

  virtual SystemSymbols* GetSymbols() = 0;

  // Returns all targets currently in the System. The returned pointers are
  // managed by the System object and should not be cached once you return to
  // the message loop.
  virtual std::vector<Target*> GetTargets() const = 0;

  // Returns all breakpoints currently in the system. The returned pointers are
  // managed by the System object and should not be cached once you return to
  // the message loop.
  virtual std::vector<Breakpoint*> GetBreakpoints() const = 0;

  // Returns the process (and hence Target) associated with the given live
  // koid. Returns 0 if not found.
  virtual Process* ProcessFromKoid(uint64_t koid) const = 0;

  // Schedules a request for the system process tree.
  virtual void GetProcessTree(ProcessTreeCallback callback) = 0;

  // Creates a new target in the system. If "clone" is given, the settings
  // from that target will be cloned into the new one. If clone is null,
  // an empty Target will be allocated.
  virtual Target* CreateNewTarget(Target* clone) = 0;

  // Creates a new breakpoint. It will have no associated process or location
  // and will be disabled.
  virtual Breakpoint* CreateNewBreakpoint() = 0;

  // Deletes the given breakpoint. The passed-in pointer will be invalid after
  // this call.
  virtual void DeleteBreakpoint(Breakpoint* breakpoint) = 0;

  // Applies to all threads of all debugged processes.
  virtual void Pause() = 0;
  virtual void Continue() = 0;

 protected:
  fxl::ObserverList<SystemObserver>& observers() { return observers_; }

 private:
  fxl::ObserverList<SystemObserver> observers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(System);
};

}  // namespace zxdb
