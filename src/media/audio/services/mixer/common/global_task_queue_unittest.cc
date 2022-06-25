// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/common/global_task_queue.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace media_audio {

namespace {
class StubTimer : public Timer {
 public:
  void SetEventBit() override { signaled_ = true; }
  void SetShutdownBit() override {}
  WakeReason SleepUntil(zx::time deadline) override { return {}; }
  void Stop() override {}

  bool signaled() const { return signaled_; }

 private:
  bool signaled_ = false;
};
}  // namespace

TEST(GlobalTaskQueueTest, Run) {
  std::vector<int> calls;
  auto make_closure = [&calls](int x) { return [&calls, x]() mutable { calls.push_back(x); }; };

  GlobalTaskQueue q;
  q.Push(kAnyThreadId, make_closure(1));
  q.Push(1, make_closure(2));
  q.Push(1, make_closure(3));
  q.Push(kAnyThreadId, make_closure(4));
  q.Push(2, make_closure(5));
  q.Push(kAnyThreadId, make_closure(6));
  q.Push(3, make_closure(7));

  // Can run the first task only.
  q.RunForThread(kAnyThreadId);
  EXPECT_THAT(calls, ::testing::ElementsAre(1));

  // Cannot run the next task on thread 2.
  q.RunForThread(2);
  EXPECT_THAT(calls, ::testing::ElementsAre(1));

  // Can run a few tasks on thread 1.
  q.RunForThread(1);
  EXPECT_THAT(calls, ::testing::ElementsAre(1, 2, 3, 4));

  // Can run a few tasks on thread 2.
  q.RunForThread(2);
  EXPECT_THAT(calls, ::testing::ElementsAre(1, 2, 3, 4, 5, 6));
}

TEST(GlobalTaskQueueTest, Notify) {
  std::vector<int> calls;
  auto make_closure = [&calls](int x) { return [&calls, x]() mutable { calls.push_back(x); }; };

  auto t1 = std::make_shared<StubTimer>();
  auto t2 = std::make_shared<StubTimer>();
  auto t3 = std::make_shared<StubTimer>();

  GlobalTaskQueue q;
  q.RegisterTimer(1, t1);
  q.RegisterTimer(2, t2);
  q.RegisterTimer(3, t3);

  // Signal t1.
  q.Push(1, make_closure(1));
  EXPECT_TRUE(t1->signaled());
  EXPECT_FALSE(t2->signaled());
  EXPECT_FALSE(t3->signaled());

  q.Push(kAnyThreadId, make_closure(2));
  q.Push(2, make_closure(3));
  EXPECT_TRUE(t1->signaled());
  EXPECT_FALSE(t2->signaled());
  EXPECT_FALSE(t3->signaled());

  // Runs the next two tasks, then signals t2
  q.RunForThread(1);
  EXPECT_THAT(calls, ::testing::ElementsAre(1, 2));
  EXPECT_TRUE(t1->signaled());
  EXPECT_TRUE(t2->signaled());
  EXPECT_FALSE(t3->signaled());

  // Run t2's task.
  q.RunForThread(2);
  EXPECT_THAT(calls, ::testing::ElementsAre(1, 2, 3));
  EXPECT_TRUE(t1->signaled());
  EXPECT_TRUE(t2->signaled());
  EXPECT_FALSE(t3->signaled());

  q.UnregisterTimer(3);

  // Do not signal t3.
  q.Push(3, make_closure(4));
  EXPECT_TRUE(t1->signaled());
  EXPECT_TRUE(t2->signaled());
  EXPECT_FALSE(t3->signaled());
}

}  // namespace media_audio
