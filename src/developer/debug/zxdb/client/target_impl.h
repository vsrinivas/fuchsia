// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_TARGET_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_TARGET_IMPL_H_

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/target_observer.h"
#include "src/developer/debug/zxdb/symbols/target_symbols.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class ProcessImpl;
class SystemImpl;

class TargetImpl : public Target {
 public:
  // The system owns this object and will outlive it.
  explicit TargetImpl(SystemImpl* system);
  ~TargetImpl() override;

  SystemImpl* system() { return system_; }
  ProcessImpl* process() { return process_.get(); }
  TargetSymbols* symbols() { return &symbols_; }

  // Allocates a new target with the same settings as this one. This isn't a real copy, because any
  // process information is not cloned.
  std::unique_ptr<TargetImpl> Clone(SystemImpl* system);

  // Notification that a new process was created from a job filter. The process will not have
  // started running yet.
  void ProcessCreatedInJob(uint64_t koid, const std::string& process_name);

  // Notification that a new process was created as a new component. We need the distinction because
  // they look identical as a process caught by a job filter.
  void ProcessCreatedAsComponent(uint64_t koid, const std::string& process_name);

  // Tests can use this to create a target for mocking purposes without making any IPC. To destroy
  // call ImplicitlyDetach().
  void CreateProcessForTesting(uint64_t koid, const std::string& process_name);

  // Removes the process from this target without making any IPC calls. This can be used to clean up
  // after a CreateProcessForTesting(), and during final shutdown. In final shutdown, we assume
  // anything still left running will continue running as-is and just clean up local references.
  //
  // If the process is not running, this will do nothing.
  void ImplicitlyDetach();

  // Target implementation:
  State GetState() const override;
  Process* GetProcess() const override;
  const TargetSymbols* GetSymbols() const override;
  const std::vector<std::string>& GetArgs() const override;
  void SetArgs(std::vector<std::string> args) override;
  void Launch(Callback callback) override;
  void Kill(Callback callback) override;
  void Attach(uint64_t koid, Callback callback) override;
  void Detach(Callback callback) override;
  void OnProcessExiting(int return_code) override;

 private:
  static void OnLaunchOrAttachReplyThunk(fxl::WeakPtr<TargetImpl> target, Callback callback,
                                         const Err& err, uint64_t koid,
                                         debug_ipc::zx_status_t status,
                                         const std::string& process_name);
  void OnLaunchOrAttachReply(Callback callback, const Err& err, uint64_t koid,
                             debug_ipc::zx_status_t status, const std::string& process_name);

  // Different status returned by the agent can mean different things.
  // ZX_ERR_IO = Process doesn't exist.
  // ZX_ERR_ALREADY_BOUND = The agent is already bound.
  void HandleAttachStatus(Callback callback, uint64_t koid, debug_ipc::zx_status_t status,
                          const std::string& process_name);

  void OnKillOrDetachReply(TargetObserver::DestroyReason reason, const Err& err,
                           debug_ipc::zx_status_t status, Callback callback);

  // Actual creation that unified common behaviour.
  std::unique_ptr<ProcessImpl> CreateProcessImpl(uint64_t koid, const std::string& name,
                                                 Process::StartType);

  SystemImpl* system_;  // Owns |this|.

  State state_ = kNone;

  std::vector<std::string> args_;

  // Associated process if there is one.
  std::unique_ptr<ProcessImpl> process_;

  TargetSymbols symbols_;

  fxl::WeakPtrFactory<TargetImpl> impl_weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TargetImpl);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_TARGET_IMPL_H_
