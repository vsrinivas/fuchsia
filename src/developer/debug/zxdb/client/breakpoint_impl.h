// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_IMPL_H_

#include <map>

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/breakpoint_action.h"
#include "src/developer/debug/zxdb/client/process_observer.h"
#include "src/developer/debug/zxdb/client/system_observer.h"
#include "src/developer/debug/zxdb/client/target_observer.h"
#include "src/developer/debug/zxdb/client/thread_observer.h"
#include "src/developer/debug/zxdb/symbols/resolve_options.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace debug_ipc {
struct BreakpointStats;
}

namespace zxdb {

class BreakpointLocationImpl;

class BreakpointImpl : public Breakpoint,
                       public TargetObserver,
                       public ProcessObserver,
                       public ThreadObserver {
 public:
  // The controller can be null in which case it will perform the default behavior. The controller
  // must outlive the breakpoint.
  BreakpointImpl(Session* session, bool is_internal);
  ~BreakpointImpl() override;

  // This flag doesn't control anything in the breakpoint but is stored here for the use of external
  // consumers. Internal breakpoints are set by the debugger internally as part of implementing
  // other features such as stepping. They should not be shown to the user.
  bool is_internal() const { return is_internal_; }

  // Identifies this breakpoint to the backend in IPC messages. This will not change.
  uint32_t backend_id() const { return backend_id_; }

  // Breakpoint implementation:
  BreakpointSettings GetSettings() const override;
  void SetSettings(const BreakpointSettings& settings,
                   fit::callback<void(const Err&)> callback) override;
  bool IsInternal() const override;
  std::vector<const BreakpointLocation*> GetLocations() const override;
  std::vector<BreakpointLocation*> GetLocations() override;

  // Called whenever new stats are available from the debug agent.
  void UpdateStats(const debug_ipc::BreakpointStats& stats);

  // Called when the backend reports that the breakpoint has been automatically
  // removed.
  void BackendBreakpointRemoved();

 private:
  friend BreakpointLocationImpl;
  struct ProcessRecord;

  // TargetObserver.
  void WillDestroyTarget(Target* target) override;

  // ProcessObserver.
  void DidCreateProcess(Process* process, bool autoattached) override;
  void WillDestroyProcess(Process* process, ProcessObserver::DestroyReason reason,
                          int exit_code) override;
  void DidLoadModuleSymbols(Process* process, LoadedModuleSymbols* module) override;
  void WillUnloadModuleSymbols(Process* process, LoadedModuleSymbols* module) override;

  // ThreadObserver.
  void WillDestroyThread(Thread* thread) override;

  void SyncBackend(fit::callback<void(const Err&)> callback = {});
  void SendBackendAddOrChange(fit::callback<void(const Err&)> callback);
  void SendBackendRemove(fit::callback<void(const Err&)> callback);

  // Notification from BreakpointLocationImpl that the enabled state has changed and the breakpoint
  // state needs to be synced.
  void DidChangeLocation();

  // Returns true if the breakpoint could possibly apply to the given process (if things like
  // symbols aren't found, it still may not necessarily apply).
  bool CouldApplyToProcess(Process* process) const;

  // Returns true if there are any enabled breakpoint locations that the backend needs to know
  // about.
  bool HasEnabledLocation() const;

  // Given a process which is new or might apply to us for the first time, Returns true if any
  // addresses were resolved.
  bool RegisterProcess(Process* process);

  // Returns the options for converting this breakpoint's input location to addresses,
  ResolveOptions GetResolveOptions() const;

  // Returns true if all input locations for this breakpoint are addesses.
  bool AllLocationsAddresses() const;

  bool is_internal_;

  // ID used to refer to this in the backend.
  const uint32_t backend_id_;

  BreakpointSettings settings_;

  debug_ipc::BreakpointStats stats_;

  // Indicates if the backend knows about this breakpoint.
  bool backend_installed_ = false;

  // Every process which this breakpoint can apply to is in this map, even if there are no addresses
  // associated with it.
  std::map<Process*, ProcessRecord> procs_;

  fxl::WeakPtrFactory<BreakpointImpl> impl_weak_factory_;

  // Set when we're a thread-scoped breakpoint and so are registered as a thread observer. There
  // are potentially a lot of threads and breakpoints, and thread-scoped breakpoints are rare, so
  // we don't regiter for these unless necessary.
  bool registered_as_thread_observer_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(BreakpointImpl);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_BREAKPOINT_IMPL_H_
