// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_THREAD_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_THREAD_H_

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/thread_controller.h"

namespace zxdb {

class MockThread : public Thread, public Stack::Delegate {
 public:
  // The process and frame pointers must outlive this class.
  explicit MockThread(Process* process)
      : Thread(process->session()), process_(process), stack_(this) {}

  // Thread implementation:
  Process* GetProcess() const override { return process_; }
  uint64_t GetKoid() const override { return 1234; }
  const std::string& GetName() const override { return thread_name_; }
  debug_ipc::ThreadRecord::State GetState() const override {
    return debug_ipc::ThreadRecord::State::kSuspended;
  }
  debug_ipc::ThreadRecord::BlockedReason GetBlockedReason() const override {
    return debug_ipc::ThreadRecord::BlockedReason::kNotBlocked;
  }
  void Pause(fit::callback<void()> on_paused) override {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [on_paused = std::move(on_paused)]() mutable { on_paused(); });
  }
  void Continue(bool forward_exception) override {}
  void ContinueWith(std::unique_ptr<ThreadController> controller,
                    fit::callback<void(const Err&)> on_continue) override {}
  void JumpTo(uint64_t new_address, fit::callback<void(const Err&)> cb) override {}
  void NotifyControllerDone(ThreadController* controller) override {}
  void StepInstruction() override {}
  const Stack& GetStack() const override { return stack_; }
  Stack& GetStack() override { return stack_; }

 private:
  // Stack::Delegate implementation.
  void SyncFramesForStack(fit::callback<void(const Err&)> callback) override {
    FX_NOTREACHED();  // All frames are available.
  }
  std::unique_ptr<Frame> MakeFrameForStack(const debug_ipc::StackFrame& input,
                                           Location location) override {
    FX_NOTREACHED();  // Should not get called since we provide stack frames.
    return std::unique_ptr<Frame>();
  }
  Location GetSymbolizedLocationForStackFrame(const debug_ipc::StackFrame& input) override {
    return Location(Location::State::kSymbolized, input.ip);
  }

  std::string thread_name_ = "test thread";
  Process* process_;

  Stack stack_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_THREAD_H_
