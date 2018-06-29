// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_impl.h"
#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/remote_api_test.h"
#include "garnet/bin/zxdb/client/thread_observer.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

class ThreadSink : public RemoteAPI {
 public:
  void set_frames_response(const debug_ipc::BacktraceReply& response) {
    frames_response_ = response;
  }

  bool breakpoint_add_called() const { return breakpoint_add_called_; }
  bool breakpoint_remove_called() const { return breakpoint_remove_called_; }

  const debug_ipc::AddOrChangeBreakpointRequest& last_breakpoint_add() const {
    return last_breakpoint_add_;
  }

  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb)
      override {
    breakpoint_add_called_ = true;
    last_breakpoint_add_ = request;
    debug_ipc::MessageLoop::Current()->PostTask(
        [cb]() { cb(Err(), debug_ipc::AddOrChangeBreakpointReply()); });
  }

  void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb)
      override {
    breakpoint_remove_called_ = true;
    debug_ipc::MessageLoop::Current()->PostTask(
        [cb]() { cb(Err(), debug_ipc::RemoveBreakpointReply()); });
  }

  void Backtrace(
      const debug_ipc::BacktraceRequest& request,
      std::function<void(const Err&, debug_ipc::BacktraceReply)> cb) override {
    // Returns the canned response.
    debug_ipc::MessageLoop::Current()->PostTask([
      cb, response = frames_response_
    ]() { cb(Err(), std::move(response)); });
  }

  void Resume(
      const debug_ipc::ResumeRequest& request,
      std::function<void(const Err&, debug_ipc::ResumeReply)> cb) override {
    // Always returns success and then quits the message loop.
    debug_ipc::MessageLoop::Current()->PostTask([cb]() {
      cb(Err(), debug_ipc::ResumeReply());
      debug_ipc::MessageLoop::Current()->QuitNow();
    });
  }

 private:
  debug_ipc::BacktraceReply frames_response_;

  bool breakpoint_add_called_ = false;
  debug_ipc::AddOrChangeBreakpointRequest last_breakpoint_add_;

  bool breakpoint_remove_called_ = false;
};

class ThreadImplTest : public RemoteAPITest {
 public:
  ThreadImplTest() = default;
  ~ThreadImplTest() override = default;

  ThreadSink& sink() { return *sink_; }

 protected:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override {
    auto sink = std::make_unique<ThreadSink>();
    sink_ = sink.get();
    return std::move(sink);
  }

 private:
  ThreadSink* sink_;  // Owned by the session.
};

class TestThreadObserver : public ThreadObserver {
 public:
  explicit TestThreadObserver(Thread* thread) : thread_(thread) {
    thread->AddObserver(this);
  }
  ~TestThreadObserver() { thread_->RemoveObserver(this); }

  bool got_stopped() const { return got_stopped_; }
  const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints() {
    return hit_breakpoints_;
  }

  void OnThreadStopped(
      Thread* thread, debug_ipc::NotifyException::Type type,
      std::vector<fxl::WeakPtr<Breakpoint>> hit_breakpoints) override {
    EXPECT_EQ(thread_, thread);
    got_stopped_ = true;
    hit_breakpoints_ = hit_breakpoints;
  }

 private:
  Thread* thread_;

  bool got_stopped_ = false;
  std::vector<fxl::WeakPtr<Breakpoint>> hit_breakpoints_;
};

}  // namespace

// Tests frame
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
  break_notification.frame.ip = kAddress1;
  break_notification.frame.sp = kStack1;
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
  sink().set_frames_response(expected_reply);

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

TEST_F(ThreadImplTest, Finish) {
  // Make a process and thread for notifying about.
  constexpr uint64_t kProcessKoid = 1234;
  InjectProcess(kProcessKoid);
  constexpr uint64_t kThreadKoid = 5678;
  Thread* thread = InjectThread(kProcessKoid, kThreadKoid);

  // Notify of thread stop.
  constexpr uint64_t kInitialAddress = 0x12345678;
  constexpr uint64_t kInitialStack = 0x1000;
  debug_ipc::NotifyException break_notification;
  break_notification.process_koid = kProcessKoid;
  break_notification.type = debug_ipc::NotifyException::Type::kSoftware;
  break_notification.thread.koid = kThreadKoid;
  break_notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  break_notification.frame.ip = kInitialAddress;
  break_notification.frame.sp = kInitialStack;
  InjectException(break_notification);

  // Supply two frames for when the thread requests them: the top one (of the
  // stop above), and the one we'll return to. This stack value should be
  // larger than above (stack grows downward).
  constexpr uint64_t kReturnAddress = 0x34567890;
  constexpr uint64_t kReturnStack = 0x1010;
  debug_ipc::BacktraceReply expected_reply;
  expected_reply.frames.resize(2);
  expected_reply.frames[0].ip = kInitialAddress;
  expected_reply.frames[0].sp = kInitialStack;
  expected_reply.frames[1].ip = kReturnAddress;
  expected_reply.frames[1].sp = kReturnStack;
  sink().set_frames_response(expected_reply);

  auto frames = thread->GetFrames();
  ASSERT_EQ(1u, frames.size());  // Should have top frame from the stop only.

  EXPECT_FALSE(sink().breakpoint_add_called());
  thread->Finish(frames[0], [](const Err&) {
    debug_ipc::MessageLoop::Current()->QuitNow();
  });
  loop().Run();

  TestThreadObserver thread_observer(thread);

  // Finish should have added a temporary breakpoint at the return address.
  // The particulars of this may change with the implementation, but it's worth
  // testing to make sure the breakpoints are all hooked up to the stepping
  // properly.
  EXPECT_TRUE(sink().breakpoint_add_called());
  ASSERT_EQ(1u, sink().last_breakpoint_add().breakpoint.locations.size());
  ASSERT_EQ(kReturnAddress,
            sink().last_breakpoint_add().breakpoint.locations[0].address);
  EXPECT_FALSE(sink().breakpoint_remove_called());

  // Simulate a hit of the breakpoint. This stack pointer is too small
  // (indicating a recursive call) so it should not trigger.
  break_notification.frame.ip = kReturnAddress;
  break_notification.frame.sp = kInitialStack - 0x100;
  break_notification.hit_breakpoints.emplace_back();
  break_notification.hit_breakpoints[0].breakpoint_id =
      sink().last_breakpoint_add().breakpoint.breakpoint_id;
  InjectException(break_notification);
  EXPECT_FALSE(thread_observer.got_stopped());

  // Simulate a breakpoint hit with a lower SP. This should trigger a thread
  // stop.
  break_notification.frame.sp = kInitialStack + 1;
  InjectException(break_notification);
  EXPECT_TRUE(thread_observer.got_stopped());
  EXPECT_TRUE(sink().breakpoint_remove_called());

  // The internal stuff is currently asynchronously deleted. It is unfortunate
  // to encode the internal lifetime management of the RunUntil object in this
  // test.
  //
  // Posting a quit to the end of the task queue and running the loop will
  // flush the pending tasks.
  debug_ipc::MessageLoop::Current()->PostTask(
      []() { debug_ipc::MessageLoop::Current()->QuitNow(); });
  loop().Run();
}

}  // namespace zxdb
