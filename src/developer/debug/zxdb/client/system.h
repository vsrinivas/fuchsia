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
#include "src/developer/debug/zxdb/client/filter_observer.h"
#include "src/developer/debug/zxdb/client/map_setting_store.h"
#include "src/developer/debug/zxdb/client/setting_store_observer.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/symbols/system_symbols.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/observer_list.h"

namespace zxdb {

class Breakpoint;
class BreakpointImpl;
class Download;
class Err;
class Filter;
class Job;
class ProcessImpl;
class SymbolServer;
class SystemObserver;
class TargetImpl;

// Represents the client's view of the system-wide state on the debugged
// computer.
class System : public ClientObject,
               public FilterObserver,
               public SettingStoreObserver,
               public SystemSymbols::DownloadHandler {
 public:
  // Callback for requesting the process tree.
  using ProcessTreeCallback = fit::callback<void(const Err&, debug_ipc::ProcessTreeReply)>;

  explicit System(Session* session);
  ~System() override;

  fxl::WeakPtr<System> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  void AddObserver(SystemObserver* observer) { observers_.AddObserver(observer); }
  void RemoveObserver(SystemObserver* observer) { observers_.RemoveObserver(observer); }

  MapSettingStore& settings() { return settings_; }

  // Provides the setting schema for this object.
  static fxl::RefPtr<SettingSchema> GetSchema();

  ProcessImpl* ProcessImplFromKoid(uint64_t koid) const;

  std::vector<TargetImpl*> GetTargetImpls() const;

  // Like CreateNewTarget but returns the implementation.
  TargetImpl* CreateNewTargetImpl(TargetImpl* clone);

  SystemSymbols* GetSymbols();

  // Returns all targets currently in this System instance. The returned pointers are managed by the
  // System object and should not be cached once you return to the message loop.  There is a single
  // default Target, which is not initially attached to anything.
  std::vector<Target*> GetTargets() const;

  // Returns all jobs currently in this System instance. The returned pointers are managed
  // by the System object and should not be cached once you return to the message loop.
  std::vector<Job*> GetJobs() const;

  // Returns all non-internal breakpoints currently in this System instance. The returned pointers
  // are managed by the System object and should not be cached once you return to the message loop.
  std::vector<Breakpoint*> GetBreakpoints() const;

  // Returns all filters currently in this System instance. The returned pointers are managed by the
  // System object and should not be cached once you return to the message loop.
  std::vector<Filter*> GetFilters() const;

  // Returns all symbol servers registered with this symbol instance. The returned pointers are
  // managed by the System object and should not be cached once you return to the message loop.
  std::vector<SymbolServer*> GetSymbolServers() const;

  // Returns the process (and hence Target) associated with the given live koid. Returns 0 if not
  // found.
  Process* ProcessFromKoid(uint64_t koid) const;

  // Schedules a request for the system process tree.
  void GetProcessTree(ProcessTreeCallback callback);

  // Creates a new target in this System instance. If "clone" is given, the settings from that
  // target will be cloned into the new one. If clone is null, an empty Target will be allocated.
  Target* CreateNewTarget(Target* clone);

  // New jobs will have no attached job.
  Job* CreateNewJob();
  void DeleteJob(Job* job);

  // Creates a new breakpoint. It will have no associated process or location and will be disabled.
  Breakpoint* CreateNewBreakpoint();

  // Creates an internal breakpoint. Internal breakpoints are not reported by GetBreakpoints() and
  // are used to implement internal stepping functions.
  Breakpoint* CreateNewInternalBreakpoint();

  // Deletes the given breakpoint. The passed-in pointer will be invalid after this call. Used for
  // both internal and external breakpoints.
  void DeleteBreakpoint(Breakpoint* breakpoint);

  // Creates a new filter. It will have no associated pattern.
  Filter* CreateNewFilter();

  // Delete a filter. The passed-in pointer will be invalid after this call.
  void DeleteFilter(Filter* filter);

  // Pauses (suspends in Zircon terms) all threads of all attached processes.
  //
  // The backend will try to ensure the threads are actually paused before issuing the on_paused
  // callback. But this is best effort and not guaranteed: both because there's a timeout for the
  // synchronous suspending and because a different continue message could race with the reply.
  void Pause(fit::callback<void()> on_paused);

  // Applies to all threads of all debugged processes.
  void Continue(bool forward);

  // Whether there's a download pending for the given build ID.
  bool HasDownload(const std::string& build_id);

  // Get a test download object.
  std::shared_ptr<Download> InjectDownloadForTesting(const std::string& build_id);

  // DownloadHandler implementation:
  void RequestDownload(const std::string& build_id, DebugSymbolFileType file_type,
                       bool quiet) override;

  // Notification that a connection has been made/terminated to a target
  // system.
  void DidConnect();
  void DidDisconnect();

  // Returns the breakpoint implementation for the given ID, or null if the ID was not found in the
  // map. This will include both internal and regular breakpoints (it is used for notification
  // dispatch).
  BreakpointImpl* BreakpointImplForId(uint32_t id);

  // SettingStoreObserver implementation.
  void OnSettingChanged(const SettingStore&, const std::string& setting_name) override;

  // Add a symbol server for testing purposes.
  void InjectSymbolServerForTesting(std::unique_ptr<SymbolServer> server);

  // Will attach to any process we are not already attached to.
  void OnFilterMatches(Job* job, const std::vector<uint64_t>& matched_pids) override;

  // Searches through for an open slot (Target without an attached process) or creates another one
  // if none is found. Calls attach on that target, passing |callback| into it.
  void AttachToProcess(uint64_t pid, Target::Callback callback);

 private:
  void AddNewTarget(std::unique_ptr<TargetImpl> target);
  void AddNewJob(std::unique_ptr<Job> job);
  void AddSymbolServer(std::unique_ptr<SymbolServer> server);

  // Called when we have attempted to download debug symbols and failed. If err is set then
  // something went wrong during the attempt, otherwise the symbols simply weren't available from
  // any of the servers.
  void NotifyFailedToFindDebugSymbols(const Err& err, const std::string& build_id,
                                      DebugSymbolFileType file_type);

  // Called when a symbol server under our control enters the Ready state.
  void OnSymbolServerBecomesReady(SymbolServer* server);

  // Called every time a new download starts.
  void DownloadStarted();

  // Called every time a download ends.
  void DownloadFinished();

  // Create a new download obect for downloading a given build ID. If quiet is set, don't report the
  // status of this download.
  //
  // If multiple callers request a download of the same build ID, this will return the same object
  // to each. The first caller's preference is taken for the quiet parameter.
  std::shared_ptr<Download> GetDownload(std::string build_id, DebugSymbolFileType file_type,
                                        bool quiet);

  // Number of symbol servers currently initializing.
  size_t servers_initializing_ = 0;

  // The number of downloads currently active.
  size_t download_count_ = 0;

  // The number of downloads that have succeeded. Every time download_count_ reaches 0, this number
  // is reported via an event, and then cleared to zero.
  size_t download_success_count_ = 0;

  // The number of downloads that have failed. Semantics are the same as download_success_count_
  size_t download_fail_count_ = 0;

  // We hold pointers to downloads while we have servers initializing so that those servers have
  // time to join the download.
  std::vector<std::shared_ptr<Download>> suspended_downloads_;

  std::vector<std::unique_ptr<SymbolServer>> symbol_servers_;
  std::vector<std::unique_ptr<TargetImpl>> targets_;
  std::vector<std::unique_ptr<Job>> jobs_;

  // Downloads currently in progress.
  std::map<std::pair<std::string, DebugSymbolFileType>, std::weak_ptr<Download>> downloads_;

  // The breakpoints are indexed by their unique backend ID. This is separate from the index
  // generated by the console frontend to describe the breakpoint noun.
  std::map<uint32_t, std::unique_ptr<BreakpointImpl>> breakpoints_;

  std::vector<std::unique_ptr<Filter>> filters_;

  SystemSymbols symbols_;

  MapSettingStore settings_;

  fxl::ObserverList<SystemObserver> observers_;

  fxl::WeakPtrFactory<System> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(System);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SYSTEM_H_
