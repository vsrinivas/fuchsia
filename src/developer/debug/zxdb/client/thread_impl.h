// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_THREAD_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_THREAD_IMPL_H_

#include <cstdint>
#include <list>

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
  std::optional<debug_ipc::ThreadRecord::State> GetState() const override;
  debug_ipc::ThreadRecord::BlockedReason GetBlockedReason() const override;
  void Pause(fit::callback<void()> on_paused) override;
  void Continue(bool forward_exception) override;
  void ContinueWith(std::unique_ptr<ThreadController> controller,
                    fit::callback<void(const Err&)> on_continue) override;
  void AddPostStopTask(PostStopTask task) override;
  void CancelAllThreadControllers() override;
  void ResumeFromAsyncThreadController(std::optional<debug_ipc::ExceptionType> type) override;
  void JumpTo(uint64_t new_address, fit::callback<void(const Err&)> cb) override;
  void NotifyControllerDone(ThreadController* controller) override;
  void StepInstructions(uint64_t count) override;
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

  // Invalidates the thread state and cached frames. Used when we know that some operation has
  // invalidated our state but we aren't sure what the new state is yet.
  void ClearState();

  // Runs the next post-stop task and queues up a continuation of this function when it has
  // completed. This will have the effect of sequentially running all of the post-stop tasks and
  // then dispatching the stop notification or continuing the program (as per |should_stop|).
  void RunNextPostStopTaskOrNotify(const StopInfo& info, bool should_stop);

  ProcessImpl* const process_;
  uint64_t koid_;

  Stack stack_;

  std::string name_;
  std::optional<debug_ipc::ThreadRecord::State> state_;
  debug_ipc::ThreadRecord::BlockedReason blocked_reason_ =
      debug_ipc::ThreadRecord::BlockedReason::kNotBlocked;

  // Ordered list of ThreadControllers that apply to this thread. This is a stack where back() is
  // the topmost controller that applies first.
  std::vector<std::unique_ptr<ThreadController>> controllers_;

  // Tasks to run when the ThreadController::OnThreadStop functions complete.
  bool handling_on_stop_ = false;
  std::list<PostStopTask> post_stop_tasks_;

  // State for thread controllers that return "kFuture" to resume from a stop later.
  //
  // This tracks the number of times a thread controller has responded "kFuture" without issuing a
  // stop or continue. This prevents infinite loops if there is a bug in the thread controllers.
  StopInfo async_stop_info_;
  int nested_stop_future_completion_ = 0;

  fxl::WeakPtrFactory<ThreadImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadImpl);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_THREAD_IMPL_H_
