// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_impl.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/remote_api_test.h"
#include "garnet/bin/zxdb/client/thread_controller.h"
#include "garnet/bin/zxdb/client/thread_impl_test_support.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

// This ThreadController always responds with "continue".
class ContinueThreadController : public ThreadController {
 public:
  // The parameter is a variable to set when the OnThreadStop() function is
  // called. It must outlive this class.
  explicit ContinueThreadController(bool* got_stop) : got_stop_(got_stop) {}
  ~ContinueThreadController() override = default;

  // ThreadController implementation.
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override {
    set_thread(thread);
    cb(Err());
  }
  ContinueOp GetContinueOp() override { return ContinueOp::Continue(); }
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override {
    *got_stop_ = true;
    return kContinue;
  }
  const char* GetName() const override { return "Continue"; }

 private:
  bool* got_stop_;
};

}  // namespace

TEST_F(ThreadImplTest, Frames) {
  // Make a process and thread for notifying about.
  constexpr uint64_t kProcessKoid = 1234;
  InjectProcess(kProcessKoid);
  constexpr uint64_t kThreadKoid = 5678;
  Thread* thread = InjectThread(kProcessKoid, kThreadKoid);

  // The thread should be in a running state with no frames.
  EXPECT_TRUE(thread->GetFrames().empty());
  EXPECT_FALSE(thread->HasAllFrames());

  // Notify of thread stop.
  constexpr uint64_t kAddress1 = 0x12345678;
  constexpr uint64_t kStack1 = 0x7890;
  debug_ipc::NotifyException break_notification;
  break_notification.process_koid = kProcessKoid;
  break_notification.type = debug_ipc::NotifyException::Type::kSoftware;
  break_notification.thread.koid = kThreadKoid;
  break_notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  break_notification.thread.frames.resize(1);
  break_notification.thread.frames[0].ip = kAddress1;
  break_notification.thread.frames[0].sp = kStack1;
  InjectException(break_notification);

  // There should be one frame with the address of the stop.
  EXPECT_FALSE(thread->HasAllFrames());
  auto frames = thread->GetFrames();
  ASSERT_EQ(1u, frames.size());
  EXPECT_EQ(kAddress1, frames[0]->GetAddress());
  EXPECT_EQ(kStack1, frames[0]->GetStackPointer());

  // Keep a weak pointer to the top stack frame. It should be preserved across
  // the updates below.
  fxl::WeakPtr<Frame> weak_top_stack = frames[0]->GetWeakPtr();
  frames.clear();

  // Construct what the full stack will be returned to the thread. The top
  // element should match the one already there.
  constexpr uint64_t kAddress2 = 0x34567890;
  constexpr uint64_t kStack2 = 0x7800;
  debug_ipc::BacktraceReply expected_reply;
  expected_reply.frames.resize(2);
  expected_reply.frames[0].ip = kAddress1;
  expected_reply.frames[0].sp = kStack1;
  expected_reply.frames[1].ip = kAddress2;
  expected_reply.frames[1].sp = kStack2;
  mock_remote_api().set_backtrace_reply(expected_reply);

  // Asynchronously request the frames. The
  thread->SyncFrames([]() { debug_ipc::MessageLoop::Current()->QuitNow(); });
  loop().Run();

  // The thread should have the new stack we provided.
  EXPECT_TRUE(thread->HasAllFrames());
  frames = thread->GetFrames();
  ASSERT_EQ(2u, frames.size());
  EXPECT_EQ(kAddress1, frames[0]->GetAddress());
  EXPECT_EQ(kStack1, frames[0]->GetStackPointer());
  EXPECT_EQ(kAddress2, frames[1]->GetAddress());
  EXPECT_EQ(kStack2, frames[1]->GetStackPointer());

  // The unchanged stack element @ index 0 should be the same Frame object.
  EXPECT_TRUE(weak_top_stack);
  EXPECT_EQ(weak_top_stack.get(), frames[0]);

  // Resuming the thread should be asynchronous so nothing should change.
  thread->Continue();
  EXPECT_EQ(2u, thread->GetFrames().size());
  EXPECT_TRUE(thread->HasAllFrames());
  EXPECT_TRUE(weak_top_stack);
  loop().Run();

  // After resuming we don't actually know what state the thread is in so
  // nothing should change. If we have better thread state notifications in
  // the future, it would be nice if the thread reported itself as running
  // and the stack was cleared at this point.

  // Stopping the thread again should clear the stack back to the one frame
  // we sent. Since the address didn't change from last time, it should be the
  // same frame object.
  InjectException(break_notification);
  EXPECT_FALSE(thread->HasAllFrames());
  frames = thread->GetFrames();
  ASSERT_EQ(1u, frames.size());
  EXPECT_EQ(kAddress1, frames[0]->GetAddress());
  EXPECT_EQ(kStack1, frames[0]->GetStackPointer());
  EXPECT_EQ(weak_top_stack.get(), frames[0]);
}

// Tests that general exceptions still run thread controllers. If the exception
// is at an address where the thread controller says "stop", that thread
// controller should be notified so it can be deleted. It doesn't matter what
// caused the stop if the thread controller thinks its done.
//
// For this exception case, the thread should always stop, even if the
// controllers say "continue."
TEST_F(ThreadImplTest, ControllersWithGeneralException) {
  constexpr uint64_t kProcessKoid = 1234;
  InjectProcess(kProcessKoid);
  constexpr uint64_t kThreadKoid = 5678;
  Thread* thread = InjectThread(kProcessKoid, kThreadKoid);

  // Notify of thread stop.
  constexpr uint64_t kAddress1 = 0x12345678;
  constexpr uint64_t kStack1 = 0x7890;
  debug_ipc::NotifyException notification;
  notification.process_koid = kProcessKoid;
  notification.type = debug_ipc::NotifyException::Type::kSoftware;
  notification.thread.koid = kThreadKoid;
  notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  notification.thread.frames.resize(1);
  notification.thread.frames[0].ip = kAddress1;
  notification.thread.frames[0].sp = kStack1;
  InjectException(notification);

  // Set a controller that always says to continue.
  bool got_stop = false;
  thread->ContinueWith(std::make_unique<ContinueThreadController>(&got_stop),
                       [](const Err& err) {});
  EXPECT_FALSE(got_stop);  // Should not have been called.

  // Start watching for thread events starting now.
  TestThreadObserver thread_observer(thread);

  // Notify on thread stop again (this is the same address as above but it
  // doesn't matter).
  notification.type = debug_ipc::NotifyException::Type::kGeneral;
  InjectException(notification);

  // The controller should have been notified and the thread should have issued
  // a stop notification even though the controller said to continue.
  EXPECT_TRUE(got_stop);
  EXPECT_TRUE(thread_observer.got_stopped());
}

}  // namespace zxdb
