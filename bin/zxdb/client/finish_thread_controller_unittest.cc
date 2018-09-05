// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/finish_thread_controller.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/client/thread_impl_test_support.h"
#include "garnet/bin/zxdb/common/err.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

class FinishThreadControllerTest : public ThreadImplTest {};

}  // namespace

TEST_F(FinishThreadControllerTest, Finish) {
  // Make a process and thread for notifying about.
  constexpr uint64_t kProcessKoid = 1234;
  InjectProcess(kProcessKoid);
  constexpr uint64_t kThreadKoid = 5678;
  Thread* thread = InjectThread(kProcessKoid, kThreadKoid);

  // Notify of thread stop.
  constexpr uint64_t kInitialAddress = 0x12345678;
  constexpr uint64_t kInitialBase = 0x1000;
  debug_ipc::NotifyException break_notification;
  break_notification.process_koid = kProcessKoid;
  break_notification.type = debug_ipc::NotifyException::Type::kSoftware;
  break_notification.thread.koid = kThreadKoid;
  break_notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  break_notification.frames.resize(1);
  break_notification.frames[0].ip = kInitialAddress;
  break_notification.frames[0].sp = kInitialBase;
  break_notification.frames[0].bp = kInitialBase;
  InjectException(break_notification);

  // Supply two frames for when the thread requests them: the top one (of the
  // stop above), and the one we'll return to. This stack value should be
  // larger than above (stack grows downward).
  constexpr uint64_t kReturnAddress = 0x34567890;
  constexpr uint64_t kReturnBase = 0x1010;
  debug_ipc::BacktraceReply expected_reply;
  expected_reply.frames.resize(2);
  expected_reply.frames[0].ip = kInitialAddress;
  expected_reply.frames[0].sp = kInitialBase;
  expected_reply.frames[0].bp = kInitialBase;
  expected_reply.frames[1].ip = kReturnAddress;
  expected_reply.frames[1].sp = kReturnBase;
  expected_reply.frames[1].bp = kReturnBase;
  sink().set_frames_response(expected_reply);

  auto frames = thread->GetFrames();
  ASSERT_EQ(1u, frames.size());  // Should have top frame from the stop only.

  EXPECT_FALSE(sink().breakpoint_add_called());
  Err out_err;
  thread->ContinueWith(std::make_unique<FinishThreadController>(
                           FinishThreadController::FromFrame(), frames[0]),
                       [&out_err](const Err& err) {
                         out_err = err;
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
  break_notification.frames.resize(1);
  break_notification.frames[0].ip = kReturnAddress;
  break_notification.frames[0].sp = kInitialBase - 0x100;
  break_notification.frames[0].bp = kInitialBase - 0x100;
  break_notification.hit_breakpoints.emplace_back();
  break_notification.hit_breakpoints[0].breakpoint_id =
      sink().last_breakpoint_add().breakpoint.breakpoint_id;
  InjectException(break_notification);
  EXPECT_FALSE(thread_observer.got_stopped());

  // Simulate a breakpoint hit with a lower BP. This should trigger a thread
  // stop.
  break_notification.frames[0].sp = kReturnBase;
  break_notification.frames[0].bp = kReturnBase;
  InjectException(break_notification);
  EXPECT_TRUE(thread_observer.got_stopped());
  EXPECT_TRUE(sink().breakpoint_remove_called());
}

}  // namespace zxdb
