// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_impl.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/remote_api_test.h"
#include "garnet/bin/zxdb/client/thread_impl_test_support.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "gtest/gtest.h"

namespace zxdb {

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

}  // namespace zxdb
