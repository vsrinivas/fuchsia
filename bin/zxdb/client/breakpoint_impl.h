// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/process_observer.h"
#include "garnet/bin/zxdb/client/target_observer.h"
#include "garnet/bin/zxdb/client/system_observer.h"
#include "garnet/public/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class BreakpointImpl : public Breakpoint,
                       public ProcessObserver,
                       public TargetObserver,
                       public SystemObserver {
 public:
  BreakpointImpl(Session* session);
  ~BreakpointImpl() override;

  // Breakpoint implementation:
  void CommitChanges(std::function<void(const Err&)> callback) override;
  bool IsEnabled() const override;
  void SetEnabled(bool enabled) override;
  Err SetScope(Scope scope, Target* target, Thread* thread) override;
  Scope GetScope(Target** target, Thread** thread) const override;
  void SetStopMode(debug_ipc::Stop stop) override;
  debug_ipc::Stop GetStopMode() const override;
  void SetAddressLocation(uint64_t address) override;
  uint64_t GetAddressLocation() const override;
  int GetHitCount() const override;

 private:
  // Attaches and detaches from the events that this object observes.
  void StopObserving();
  void StartObserving();

  // ProcessObserver.
  void WillDestroyThread(Process* process, Thread* thread) override;

  // TargetObserver.
  void DidCreateProcess(Target* target, Process* process) override;
  void DidDestroyProcess(Target* target, DestroyReason reason,
                         int exit_code) override;

  // SystemObserver.
  void WillDestroyTarget(Target* target) override;

  void SendAddOrChange(Process* process,
                       std::function<void(const Err&)> callback);
  void SendBreakpointRemove(Process* process,
                            std::function<void(const Err&)> callback);

  bool enabled_ = false;

  // TODO(brettw) this assumes exactly one backend breakpoint in one process.
  // A symbolic breakpoitn can have multiple locations and be in multiple
  // processes. 0 means no current backend breakpoint.
  uint32_t backend_id_ = 0;

  Scope scope_ = Scope::kSystem;
  Target* target_scope_ = nullptr;  // Valid when scope == kTarget or kThread.
  Thread* thread_scope_ = nullptr;  // Valid when scope == kThread.

  debug_ipc::Stop stop_mode_ = debug_ipc::Stop::kAll;
  int hit_count_ = 0;

  uint64_t address_ = 0;

  bool is_system_observer_ = false;
  bool is_target_observer_ = false;
  bool is_process_observer_ = false;

  fxl::WeakPtrFactory<BreakpointImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BreakpointImpl);
};

}  // namespace zxdb
