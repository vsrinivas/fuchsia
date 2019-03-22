// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_ZXDB_CLIENT_SYSTEM_H_
#define GARNET_BIN_ZXDB_CLIENT_SYSTEM_H_

#include <functional>
#include <vector>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/bin/zxdb/client/job_context.h"
#include "garnet/bin/zxdb/client/setting_store.h"
#include "garnet/bin/zxdb/client/setting_store_observer.h"
#include "garnet/bin/zxdb/client/target.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/observer_list.h"
#include "src/developer/debug/ipc/protocol.h"

namespace zxdb {

class Breakpoint;
class Err;
class SystemObserver;
class SystemSymbols;

// Represents the client's view of the system-wide state on the debugged
// computer.
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

  // Returns all targets currently in this System instance. The returned
  // pointers are managed by the System object and should not be cached once you
  // return to the message loop.  There is a single default Target, which is not
  // initially attached to anything.
  virtual std::vector<Target*> GetTargets() const = 0;

  // Returns all job contexts currently in this System instance. The returned
  // pointers are managed by the System object and should not be cached once you
  // return to the message loop.
  virtual std::vector<JobContext*> GetJobContexts() const = 0;

  // Returns all non-internal breakpoints currently in this System instance. The
  // returned pointers are managed by the System object and should not be cached
  // once you return to the message loop.
  virtual std::vector<Breakpoint*> GetBreakpoints() const = 0;

  // Returns the process (and hence Target) associated with the given live
  // koid. Returns 0 if not found.
  virtual Process* ProcessFromKoid(uint64_t koid) const = 0;

  // Schedules a request for the system process tree.
  virtual void GetProcessTree(ProcessTreeCallback callback) = 0;

  // Creates a new target in this System instance. If "clone" is given, the
  // settings from that target will be cloned into the new one. If clone is
  // null, an empty Target will be allocated.
  virtual Target* CreateNewTarget(Target* clone) = 0;

  // Creates a new job context in this System instance. If "clone" is given, the
  // settings from that target will be cloned into the new one. If clone is
  // null, an empty Target will be allocated.
  virtual JobContext* CreateNewJobContext(JobContext* clone) = 0;

  // Creates a new breakpoint. It will have no associated process or location
  // and will be disabled.
  virtual Breakpoint* CreateNewBreakpoint() = 0;

  // Creates an internal breakpoint. Internal breakpoints are not reported by
  // GetBreakpoints() and are used to implement internal stepping functions.
  virtual Breakpoint* CreateNewInternalBreakpoint() = 0;

  // Deletes the given breakpoint. The passed-in pointer will be invalid after
  // this call. Used for both internal and external breakpoints.
  virtual void DeleteBreakpoint(Breakpoint* breakpoint) = 0;

  // Applies to all threads of all debugged processes.
  virtual void Pause() = 0;
  virtual void Continue() = 0;

  // Provides the setting schema for this object.
  static fxl::RefPtr<SettingSchema> GetSchema();

  SettingStore& settings() { return settings_; }

 protected:
  fxl::ObserverList<SystemObserver>& observers() { return observers_; }

  SettingStore settings_;

 private:
  fxl::ObserverList<SystemObserver> observers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(System);
};

}  // namespace zxdb

#endif  // GARNET_BIN_ZXDB_CLIENT_SYSTEM_H_
