// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/public/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Breakpoint;
class FrameImpl;
class ProcessImpl;

class ThreadImpl : public Thread {
 public:
  ThreadImpl(ProcessImpl* process, const debug_ipc::ThreadRecord& record);
  ~ThreadImpl() override;

  ProcessImpl* process() const { return process_; }

  // Thread implementation:
  Process* GetProcess() const override;
  uint64_t GetKoid() const override;
  const std::string& GetName() const override;
  debug_ipc::ThreadRecord::State GetState() const override;
  void Pause() override;
  void Continue() override;
  void ContinueWith(std::unique_ptr<ThreadController> controller,
                    std::function<void(const Err&)> on_continue) override;
  void NotifyControllerDone(ThreadController* controller) override;
  Err Step() override;
  void StepInstruction() override;
  std::vector<Frame*> GetFrames() const override;
  bool HasAllFrames() const override;
  void SyncFrames(std::function<void()> callback) override;

  void GetRegisters(
      std::function<void(const Err&, const RegisterSet&)>) override;

  // NOTE: If the registers are not up to date, the set can be null.
  const RegisterSet* registers() const { return registers_.get(); }

  // Updates the thread metadata with new state from the agent. Neither
  // function issues any notifications. When an exception is hit for example,
  // everything needs to be updated first to a consistent state and then we
  // issue notifications.
  void SetMetadata(const debug_ipc::ThreadRecord& record);
  void SetMetadataFromException(const debug_ipc::NotifyException& notify);

  // Notification of an exception. Call after SetMetadataFromException() in
  // cases where a stop may be required. This function will check controllers
  // and will either stop (dispatching notifications) or transparently
  // continue accordingly.
  //
  // The his breakpoints should include all breakpoints, including internal
  // ones.
  void OnException(
      debug_ipc::NotifyException::Type type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints);

 private:
  // Saves the new frames for this thread.
  void SaveFrames(const std::vector<debug_ipc::StackFrame>& frames,
                  bool have_all);

  // Invlidates the cached frames.
  void ClearFrames();

  ProcessImpl* const process_;
  uint64_t koid_;
  std::unique_ptr<RegisterSet> registers_;
  std::string name_;
  debug_ipc::ThreadRecord::State state_;

  std::vector<std::unique_ptr<FrameImpl>> frames_;
  bool has_all_frames_ = false;

  // Ordered list of ThreadControllers that apply to this thread. This is
  // a stack where back() is the topmost contoller that applies first.
  std::vector<std::unique_ptr<ThreadController>> controllers_;

  fxl::WeakPtrFactory<ThreadImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadImpl);
};

}  // namespace zxdb
