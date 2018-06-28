// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/breakpoint_action.h"
#include "garnet/bin/zxdb/client/process_observer.h"
#include "garnet/bin/zxdb/client/system_observer.h"
#include "garnet/bin/zxdb/client/target_observer.h"
#include "garnet/public/lib/fxl/memory/weak_ptr.h"

namespace debug_ipc {
struct BreakpointStats;
}

namespace zxdb {

class BreakpointController;
class BreakpointLocationImpl;

class BreakpointImpl : public Breakpoint,
                       public ProcessObserver,
                       public SystemObserver {
 public:
  // The controller can be null in which case it will perform the default
  // behavior. The controller must outlive the breakpoint.
  BreakpointImpl(Session* session, bool is_internal,
                 BreakpointController* controller = nullptr);
  ~BreakpointImpl() override;

  // This flag doesn't control anything in the breakpoint but is stored here
  // for the use of external consumers. Internal breakpoints are set by the
  // debugger internally as part of implementing other features such as
  // stepping. They should not be shown to the user.
  bool is_internal() const { return is_internal_; }

  // Identifies this breakpoint to the backend in IPC messages. This will not
  // change.
  uint32_t backend_id() const { return backend_id_; }

  // Breakpoint implementation:
  BreakpointSettings GetSettings() const override;
  void SetSettings(const BreakpointSettings& settings,
                   std::function<void(const Err&)> callback) override;
  std::vector<BreakpointLocation*> GetLocations() override;

  // Called whenever new stats are available from the debug agent.
  void UpdateStats(const debug_ipc::BreakpointStats& stats);

  // Called when the breakpoint is hit on the given thread. The thread state
  // and breakpoint status should already have been upated to reflect the new
  // stopped state, but no notifications should have been issued yet (because
  // the return value may be "continue".
  //
  // The return value indicates what this breakpoint thinks should happen as a
  // result. This isn't guaranteed since multiple breakpoints can exist at the
  // same address and they can have different answers.
  BreakpointAction OnHit(Thread* thread);

  // Called when the backend reports that the breakpoint has been automatically
  // removed.
  void BackendBreakpointRemoved();

 private:
  friend BreakpointLocationImpl;
  struct ProcessRecord;

  // ProcessObserver.
  void WillDestroyThread(Process* process, Thread* thread) override;
  void DidLoadModuleSymbols(Process* process,
                            LoadedModuleSymbols* module) override;
  void WillUnloadModuleSymbols(Process* process,
                               LoadedModuleSymbols* module) override;

  // SystemObserver.
  void WillDestroyTarget(Target* target) override;
  void GlobalDidCreateProcess(Process* process) override;
  void GlobalWillDestroyProcess(Process* process) override;

  void SyncBackend(std::function<void(const Err&)> callback =
                       std::function<void(const Err&)>());
  void SendBackendAddOrChange(std::function<void(const Err&)> callback);
  void SendBackendRemove(std::function<void(const Err&)> callback);

  // Notification from BreakpointLocationImpl that the enabled state has
  // changed and the breakpoint state needs to be synced.
  void DidChangeLocation();

  // Returns true if the breakpoint could possibly apply to the given process
  // (if things like symbols aren't found, it still may not necessarily apply).
  bool CouldApplyToProcess(Process* process) const;

  // Returns true if there are any enabled breakpoint locations that the
  // backend needs to know about.
  bool HasEnabledLocation() const;

  // Given a process which is new or might apply to us for the first time,
  // Returns true if any addresses were resolved.
  bool RegisterProcess(Process* process);

  BreakpointController* const controller_;  // Non-owning.

  bool is_internal_;

  // ID used to refer to this in the backend.
  const uint32_t backend_id_;

  BreakpointSettings settings_;

  debug_ipc::BreakpointStats stats_;

  // Indicates if the backend knows about this breakpoint.
  bool backend_installed_ = false;

  // Every process which this breakpoint can apply to is in this map, even if
  // there are no addresses associated with it.
  std::map<Process*, ProcessRecord> procs_;

  fxl::WeakPtrFactory<BreakpointImpl> impl_weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BreakpointImpl);
};

}  // namespace zxdb
