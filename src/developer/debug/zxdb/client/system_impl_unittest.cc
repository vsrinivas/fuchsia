// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/system_impl.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/thread.h"

namespace zxdb {

namespace {

class SystemImplTest : public RemoteAPITest {};

}  // namespace

// Tests that thread state is updated when doing a system-wide continue.
TEST_F(SystemImplTest, GlobalContinue) {
  // Make a process and thread for notifying about.
  constexpr uint64_t kProcessKoid = 1234;
  InjectProcess(kProcessKoid);
  constexpr uint64_t kThread1Koid = 5678;
  Thread* thread1 = InjectThread(kProcessKoid, kThread1Koid);
  constexpr uint64_t kThread2Koid = 9012;
  Thread* thread2 = InjectThread(kProcessKoid, kThread2Koid);
  mock_remote_api()->GetAndResetResumeCount();  // Clear from thread init.

  constexpr uint64_t kAddress = 0x12345678;
  constexpr uint64_t kStack = 0x7890;

  // Notify of thread stop on thread 1.
  debug_ipc::NotifyException break_notification;
  break_notification.type = debug_ipc::NotifyException::Type::kSoftware;
  break_notification.thread.process_koid = kProcessKoid;
  break_notification.thread.thread_koid = kThread1Koid;
  break_notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  break_notification.thread.frames.emplace_back(kAddress, kStack, kStack);
  InjectException(break_notification);
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());

  // Same on thread 2.
  break_notification.thread.thread_koid = kThread2Koid;
  InjectException(break_notification);

  // Continue globally. This should in turn update the thread.
  session().system().Continue();

  // Both threads should have been resumed in the backend.
  EXPECT_EQ(2, mock_remote_api()->GetAndResetResumeCount());

  // The threads should have no stack.
  EXPECT_FALSE(thread1->GetStack().has_all_frames());
  ASSERT_EQ(0u, thread1->GetStack().size());
  EXPECT_FALSE(thread2->GetStack().has_all_frames());
  ASSERT_EQ(0u, thread2->GetStack().size());
}

}  // namespace zxdb
