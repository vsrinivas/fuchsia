// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_THREAD_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_THREAD_IMPL_H_

#include "gtest/gtest_prod.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Breakpoint;
class FrameImpl;
class ProcessImpl;

class ThreadImpl final : public Thread, public Stack::Delegate {
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
  void Pause(fit::callback<void()> on_paused) override;
  void Continue(bool forward_exception) override;
  void ContinueWith(std::unique_ptr<ThreadController> controller,
                    fit::callback<void(const Err&)> on_continue) override;
  void JumpTo(uint64_t new_address, fit::callback<void(const Err&)> cb) override;
  void NotifyControllerDone(ThreadController* controller) override;
  void StepInstruction() override;
  const Stack& GetStack() const override;
  Stack& GetStack() override;

  // Updates the thread metadata with new state from the agent. Does not issue any notifications.
  // When an exception is hit for example, everything needs to be updated first to a consistent
  // state and then we issue notifications.
  void SetMetadata(const debug_ipc::ThreadRecord& record);

  // Notification of an exception. Call after SetMetadata() in cases where a stop may be required.
  // This function will check controllers and will either stop (dispatching notifications) or
  // transparently continue accordingly.
  //
  // The breakpoints will include all breakpoints, including internal ones.
  void OnException(const StopInfo& info);

 private:
  FRIEND_TEST(ThreadImplTest, StopNoStack);

  // Stack::Delegate implementation.
  void SyncFramesForStack(fit::callback<void(const Err&)> callback) override;
  std::unique_ptr<Frame> MakeFrameForStack(const debug_ipc::StackFrame& input,
                                           Location location) override;
  Location GetSymbolizedLocationForStackFrame(const debug_ipc::StackFrame& input) override;

  // Invalidates the cached frames.
  void ClearFrames();

  ProcessImpl* const process_;
  uint64_t koid_;

  Stack stack_;

  std::string name_;
  debug_ipc::ThreadRecord::State state_;
  debug_ipc::ThreadRecord::BlockedReason blocked_reason_;

  // Ordered list of ThreadControllers that apply to this thread. This is a stack where back() is
  // the topmost controller that applies first.
  std::vector<std::unique_ptr<ThreadController>> controllers_;

  fxl::WeakPtrFactory<ThreadImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadImpl);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_THREAD_IMPL_H_
