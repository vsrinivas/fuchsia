// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SYSTEM_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SYSTEM_H_

#include <memory>
#include <vector>

#include "lib/fit/function.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/client/client_object.h"
#include "src/developer/debug/zxdb/client/job_context.h"
#include "src/developer/debug/zxdb/client/setting_store.h"
#include "src/developer/debug/zxdb/client/setting_store_observer.h"
#include "src/developer/debug/zxdb/client/symbol_server.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/observer_list.h"

namespace zxdb {

class Breakpoint;
class Download;
class Err;
class Filter;
class SystemObserver;
class SystemSymbols;

// Represents the client's view of the system-wide state on the debugged
// computer.
class System : public ClientObject {
 public:
  // Callback for requesting the process tree.
  using ProcessTreeCallback = fit::callback<void(const Err&, debug_ipc::ProcessTreeReply)>;

  explicit System(Session* session);
  ~System() override;

  fxl::WeakPtr<System> GetWeakPtr();

  void AddObserver(SystemObserver* observer);
  void RemoveObserver(SystemObserver* observer);

  virtual SystemSymbols* GetSymbols() = 0;

  // Returns all targets currently in this System instance. The returned pointers are managed by the
  // System object and should not be cached once you return to the message loop.  There is a single
  // default Target, which is not initially attached to anything.
  virtual std::vector<Target*> GetTargets() const = 0;

  // Returns all job contexts currently in this System instance. The returned pointers are managed
  // by the System object and should not be cached once you return to the message loop.
  virtual std::vector<JobContext*> GetJobContexts() const = 0;

  // Returns all non-internal breakpoints currently in this System instance. The returned pointers
  // are managed by the System object and should not be cached once you return to the message loop.
  virtual std::vector<Breakpoint*> GetBreakpoints() const = 0;

  // Returns all filters currently in this System instance. The returned pointers are managed by the
  // System object and should not be cached once you return to the message loop.
  virtual std::vector<Filter*> GetFilters() const = 0;

  // Returns all symbol servers registered with this symbol instance. The returned pointers are
  // managed by the System object and should not be cached once you return to the message loop.
  virtual std::vector<SymbolServer*> GetSymbolServers() const = 0;

  // Returns the process (and hence Target) associated with the given live koid. Returns 0 if not
  // found.
  virtual Process* ProcessFromKoid(uint64_t koid) const = 0;

  // Schedules a request for the system process tree.
  virtual void GetProcessTree(ProcessTreeCallback callback) = 0;

  // Creates a new target in this System instance. If "clone" is given, the settings from that
  // target will be cloned into the new one. If clone is null, an empty Target will be allocated.
  virtual Target* CreateNewTarget(Target* clone) = 0;

  // Creates a new job context in this System instance. If "clone" is given, the settings from that
  // target will be cloned into the new one. If clone is null, an empty Target will be allocated.
  virtual JobContext* CreateNewJobContext(JobContext* clone) = 0;

  // Creates a new breakpoint. It will have no associated process or location and will be disabled.
  virtual Breakpoint* CreateNewBreakpoint() = 0;

  // Creates an internal breakpoint. Internal breakpoints are not reported by GetBreakpoints() and
  // are used to implement internal stepping functions.
  virtual Breakpoint* CreateNewInternalBreakpoint() = 0;

  // Deletes the given breakpoint. The passed-in pointer will be invalid after this call. Used for
  // both internal and external breakpoints.
  virtual void DeleteBreakpoint(Breakpoint* breakpoint) = 0;

  // Creates a new filter. It will have no associated pattern.
  virtual Filter* CreateNewFilter() = 0;

  // Delete a filter. The passed-in pointer will be invalid after this call.
  virtual void DeleteFilter(Filter* filter) = 0;

  // Pauses (suspends in Zircon terms) all threads of all attached processes.
  //
  // The backend will try to ensure the threads are actually paused before issuing the on_paused
  // callback. But this is best effort and not guaranteed: both because there's a timeout for the
  // synchronous suspending and because a different continue message could race with the reply.
  virtual void Pause(fit::callback<void()> on_paused) = 0;

  // Applies to all threads of all debugged processes.
  virtual void Continue() = 0;

  // Whether there's a download pending for the given build ID.
  virtual bool HasDownload(const std::string& build_id) { return false; }

  // Get a test download object.
  virtual std::shared_ptr<Download> InjectDownloadForTesting(const std::string& build_id) {
    return nullptr;
  }

  // Provides the setting schema for this object.
  static fxl::RefPtr<SettingSchema> GetSchema();

  SettingStore& settings() { return settings_; }

 protected:
  fxl::ObserverList<SystemObserver>& observers() { return observers_; }

  SettingStore settings_;

 private:
  fxl::ObserverList<SystemObserver> observers_;

  fxl::WeakPtrFactory<System> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(System);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SYSTEM_H_
