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
  debug_ipc::ThreadRecord::BlockedReason GetBlockedReason() const override;
  void Pause() override;
  void Continue() override;
  void ContinueWith(std::unique_ptr<ThreadController> controller,
                    std::function<void(const Err&)> on_continue) override;
  void NotifyControllerDone(ThreadController* controller) override;
  void StepInstruction() override;
  const std::vector<Frame*>& GetFrames() const override;
  bool HasAllFrames() const override;
  void SyncFrames(std::function<void()> callback) override;
  FrameFingerprint GetFrameFingerprint(size_t frame_index) const override;
  void GetRegisters(
      std::vector<debug_ipc::RegisterCategory::Type> cats_to_get,
      std::function<void(const Err&, const RegisterSet&)>) override;

  // NOTE: If the registers are not up to date, the set can be null.
  const RegisterSet* registers() const { return registers_.get(); }

  // Updates the thread metadata with new state from the agent. Does not issue
  // any notifications. When an exception is hit for example, everything needs
  // to be updated first to a consistent state and then we issue notifications.
  void SetMetadata(const debug_ipc::ThreadRecord& record);

  // Notification of an exception. Call after SetMetadata() in cases where a
  // stop may be required. This function will check controllers and will either
  // stop (dispatching notifications) or transparently continue accordingly.
  //
  // The his breakpoints should include all breakpoints, including internal
  // ones.
  void OnException(
      debug_ipc::NotifyException::Type type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints);

 private:
  // Invalidates the cached frames.
  void ClearFrames();

  ProcessImpl* const process_;
  uint64_t koid_;

  // Register state queried from the DebugAgent.
  // NOTE: Depending on the request, it could be that the register set does
  //       not hold the complete register state from the CPU (eg. it could be
  //       missing the vector or debug registers).
  std::unique_ptr<RegisterSet> registers_;
  std::string name_;
  debug_ipc::ThreadRecord::State state_;
  debug_ipc::ThreadRecord::BlockedReason blocked_reason_;

  std::vector<std::unique_ptr<FrameImpl>> frames_;
  bool has_all_frames_ = false;

  // Cached version of frames_ containing non-owning pointers to the base type.
  // This is the backing store for GetFrames() which can be called frequently
  // so we don't want to return a copy every time.
  //
  // When empty, it should be repopulated from frames_.
  mutable std::vector<Frame*> frames_cache_;

  // Ordered list of ThreadControllers that apply to this thread. This is
  // a stack where back() is the topmost controller that applies first.
  std::vector<std::unique_ptr<ThreadController>> controllers_;

  fxl::WeakPtrFactory<ThreadImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadImpl);
};

}  // namespace zxdb
