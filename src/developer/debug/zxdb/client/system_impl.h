// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SYSTEM_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SYSTEM_IMPL_H_

#include <map>
#include <memory>
#include <vector>

#include "src/developer/debug/zxdb/client/filter_observer.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/symbols/system_symbols.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class BreakpointImpl;
class Download;
class JobContextImpl;
class ProcessImpl;
class SystemSymbolsProxy;
class TargetImpl;

class SystemImpl final : public System,
                         public FilterObserver,
                         public SettingStoreObserver,
                         public SystemSymbols::DownloadHandler {
 public:
  explicit SystemImpl(Session* session);
  ~SystemImpl() override;

  ProcessImpl* ProcessImplFromKoid(uint64_t koid) const;

  std::vector<TargetImpl*> GetTargetImpls() const;

  // Like CreateNewTarget byt returns the implementation.
  TargetImpl* CreateNewTargetImpl(TargetImpl* clone);

  // System implementation:
  SystemSymbols* GetSymbols() override;
  std::vector<Target*> GetTargets() const override;
  std::vector<JobContext*> GetJobContexts() const override;
  std::vector<Breakpoint*> GetBreakpoints() const override;
  std::vector<Filter*> GetFilters() const override;
  std::vector<SymbolServer*> GetSymbolServers() const override;
  Process* ProcessFromKoid(uint64_t koid) const override;
  void GetProcessTree(ProcessTreeCallback callback) override;
  Target* CreateNewTarget(Target* clone) override;
  JobContext* CreateNewJobContext() override;
  void DeleteJobContext(JobContext* job_context) override;
  Breakpoint* CreateNewBreakpoint() override;
  Breakpoint* CreateNewInternalBreakpoint() override;
  void DeleteBreakpoint(Breakpoint* breakpoint) override;
  Filter* CreateNewFilter() override;
  void DeleteFilter(Filter* filter) override;
  void Pause(fit::callback<void()> on_paused) override;
  void Continue() override;
  bool HasDownload(const std::string& build_id) override;
  std::shared_ptr<Download> InjectDownloadForTesting(const std::string& build_id) override;

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
  void OnFilterMatches(JobContext* job, const std::vector<uint64_t>& matched_pids) override;

  // Searches through for an open slot (Target without an attached process) or creates another one
  // if none is found. Calls attach on that target, passing |callback| into it.
  void AttachToProcess(uint64_t pid, Target::Callback callback);

 private:
  void AddNewTarget(std::unique_ptr<TargetImpl> target);
  void AddNewJobContext(std::unique_ptr<JobContextImpl> job_context);

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

  // Called when we get a new server and it is still initializing.
  void ServerStartedInitializing();

  // Called when a new server is no longer initializing.
  void ServerFinishedInitializing();

  // Create a new download obect for downloading a given build ID. If quiet is set, don't report the
  // status of this download.
  //
  // If multiple callers request a download of the same build ID, this will return the same object
  // to each. The first caller's preference is taken for the quiet parameter.
  std::shared_ptr<Download> GetDownload(std::string build_id, DebugSymbolFileType file_type,
                                        bool quiet);

  // Set up a symbol server after it has been added to symbol_servers_.
  void AddSymbolServer(SymbolServer* server);

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
  std::vector<std::unique_ptr<JobContextImpl>> job_contexts_;

  // Downloads currently in progress.
  std::map<std::pair<std::string, DebugSymbolFileType>, std::weak_ptr<Download>> downloads_;

  // The breakpoints are indexed by their unique backend ID. This is separate from the index
  // generated by the console frontend to describe the breakpoint noun.
  std::map<uint32_t, std::unique_ptr<BreakpointImpl>> breakpoints_;

  std::vector<std::unique_ptr<Filter>> filters_;

  SystemSymbols symbols_;

  fxl::WeakPtrFactory<SystemImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SystemImpl);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SYSTEM_IMPL_H_
