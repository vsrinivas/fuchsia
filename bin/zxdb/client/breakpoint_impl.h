// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/process_observer.h"
#include "garnet/bin/zxdb/client/system_observer.h"
#include "garnet/bin/zxdb/client/target_observer.h"
#include "garnet/public/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class BreakpointLocationImpl;

class BreakpointImpl : public Breakpoint,
                       public ProcessObserver,
                       public SystemObserver {
 public:
  explicit BreakpointImpl(Session* session);
  ~BreakpointImpl() override;

  // Breakpoint implementation:
  BreakpointSettings GetSettings() const override;
  void SetSettings(const BreakpointSettings& settings,
                   std::function<void(const Err&)> callback) override;
  std::vector<BreakpointLocation*> GetLocations() override;

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

  BreakpointSettings settings_;

  // Every process which this breakpoint can apply to is in this map, even if
  // there are no addresses assocaited with it.
  std::map<Process*, ProcessRecord> procs_;

  // ID used to refer to this in the backend. 0 means no current backend
  // breakpoint.
  uint32_t backend_id_ = 0;

  fxl::WeakPtrFactory<BreakpointImpl> impl_weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BreakpointImpl);
};

}  // namespace zxdb
