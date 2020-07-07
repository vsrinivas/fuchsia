// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flib/fence_queue.h"

#include <lib/async-testing/test_loop.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/ui/lib/escher/test/flib/util.h"

namespace escher {
namespace test {

TEST(FenceQueueTest, QueueTaskWithNoFences) {
  async::TestLoop loop;

  bool task_complete = false;
  auto task = [&task_complete] { task_complete = true; };

  auto fence_queue = std::make_shared<FenceQueue>();
  fence_queue->QueueTask(std::move(task), {});

  EXPECT_FALSE(task_complete);
  loop.RunUntilIdle();
  EXPECT_TRUE(task_complete);
}

TEST(FenceQueueTest, QueueTaskWithFence) {
  async::TestLoop loop;

  bool task_complete = false;
  auto task = [&task_complete] { task_complete = true; };

  auto fence_queue = std::make_shared<FenceQueue>();

  std::vector<zx::event> events;
  zx::event fence;
  zx::event::create(0, &fence);
  events.emplace_back(CopyEvent(fence));
  fence_queue->QueueTask(std::move(task), std::move(events));

  EXPECT_FALSE(task_complete);
  loop.RunUntilIdle();
  EXPECT_FALSE(task_complete);

  fence.signal(0u, ZX_EVENT_SIGNALED);
  loop.RunUntilIdle();

  EXPECT_TRUE(task_complete);
}

// Queues a task with multiple fences and checks that the task doesn't run until all fences have
// been signaled.
TEST(FenceQueueTest, QueueTaskWithMultipleFences) {
  async::TestLoop loop;

  bool task_complete = false;
  auto task = [&task_complete] { task_complete = true; };

  auto fence_queue = std::make_shared<FenceQueue>();

  std::vector<zx::event> events;
  zx::event fence1;
  zx::event::create(0, &fence1);
  events.emplace_back(CopyEvent(fence1));
  zx::event fence2;
  zx::event::create(0, &fence2);
  events.emplace_back(CopyEvent(fence2));
  fence_queue->QueueTask(std::move(task), std::move(events));

  EXPECT_FALSE(task_complete);
  loop.RunUntilIdle();
  EXPECT_FALSE(task_complete);

  // Signal fences out of order, since it shouldn't matter.
  fence2.signal(0u, ZX_EVENT_SIGNALED);
  loop.RunUntilIdle();
  EXPECT_FALSE(task_complete);

  fence1.signal(0u, ZX_EVENT_SIGNALED);
  loop.RunUntilIdle();
  EXPECT_TRUE(task_complete);
}

// Queues two tasks, then signals the fences in order and confirms that the updates are handled as
// their indiviual fences are signaled.
TEST(FenceQueueTest, QueueMultipleTasksWithFences_SignalledInOrder) {
  async::TestLoop loop;

  bool task1_complete = false;
  bool task2_complete = false;

  auto fence_queue = std::make_shared<FenceQueue>();

  zx::event fence1;
  {
    std::vector<zx::event> events;
    zx::event::create(0, &fence1);
    events.emplace_back(CopyEvent(fence1));
    fence_queue->QueueTask([&task1_complete] { task1_complete = true; }, std::move(events));
  }

  zx::event fence2;
  {
    std::vector<zx::event> events;
    zx::event::create(0, &fence2);
    events.emplace_back(CopyEvent(fence2));
    fence_queue->QueueTask([&task2_complete] { task2_complete = true; }, std::move(events));
  }

  loop.RunUntilIdle();
  EXPECT_FALSE(task1_complete);
  EXPECT_FALSE(task2_complete);

  fence1.signal(0u, ZX_EVENT_SIGNALED);
  loop.RunUntilIdle();
  EXPECT_TRUE(task1_complete);
  EXPECT_FALSE(task2_complete);

  fence2.signal(0u, ZX_EVENT_SIGNALED);
  loop.RunUntilIdle();
  EXPECT_TRUE(task1_complete);
  EXPECT_TRUE(task2_complete);
}

// Queues two tasks, then signals their fences out order and confirms that no task is completed
// before the first task's fence is signaled.
TEST(FenceQueueTest, QueueMultipleTasksWithFences_SignalledOutOfOrder) {
  async::TestLoop loop;

  bool task1_complete = false;
  bool task2_complete = false;

  auto fence_queue = std::make_shared<FenceQueue>();

  zx::event fence1;
  {
    std::vector<zx::event> events;
    zx::event::create(0, &fence1);
    events.emplace_back(CopyEvent(fence1));
    fence_queue->QueueTask([&task1_complete] { task1_complete = true; }, std::move(events));
  }

  zx::event fence2;
  {
    std::vector<zx::event> events;
    zx::event::create(0, &fence2);
    events.emplace_back(CopyEvent(fence2));
    fence_queue->QueueTask([&task2_complete] { task2_complete = true; }, std::move(events));
  }

  loop.RunUntilIdle();
  EXPECT_FALSE(task1_complete);
  EXPECT_FALSE(task2_complete);

  // fence2 signalled, but task1 hasn't completed so should not run yet.
  fence2.signal(0u, ZX_EVENT_SIGNALED);
  loop.RunUntilIdle();
  EXPECT_FALSE(task1_complete);
  EXPECT_FALSE(task2_complete);

  // All fences signalled, all tasks should run.
  fence1.signal(0u, ZX_EVENT_SIGNALED);
  loop.RunUntilIdle();
  EXPECT_TRUE(task1_complete);
  EXPECT_TRUE(task2_complete);
}

// Test that destroys the FenceQueue inside a task and confirms that it terminates gracefully.
TEST(FenceQueueTest, DestroyFenceQueueInTask) {
  async::TestLoop loop;

  bool task1_complete = false;
  bool task2_complete = false;
  auto fence_queue = std::make_shared<FenceQueue>();

  // Keep only a weak reference outside the closure.
  std::weak_ptr<FenceQueue> weak = fence_queue;

  EXPECT_FALSE(weak.expired());
  fence_queue->QueueTask(/*task*/
                         [&task1_complete, shared = std::move(fence_queue)]() mutable {
                           task1_complete = true;
                           shared.reset();
                         },
                         /*fences*/ {});

  // Should not fire, since the queue should be destroyed before this.
  weak.lock()->QueueTask(/*task*/ [&task2_complete] { task2_complete = true; }, /*fences*/ {});

  loop.RunUntilIdle();
  EXPECT_TRUE(task1_complete);
  EXPECT_FALSE(task2_complete);
  EXPECT_TRUE(weak.expired());
}

// Test that destroys the FenceQueue after a task should have been put on the looper and ensures it
// doesn't run.
TEST(FenceQueueTest, DestroyFenceQueueBeforeTask) {
  async::TestLoop loop;

  bool task_complete = false;
  auto fence_queue = std::make_shared<FenceQueue>();

  fence_queue->QueueTask(/*task*/ [&task_complete]() { task_complete = true; }, /*fences*/ {});

  fence_queue.reset();

  loop.RunUntilIdle();
  EXPECT_FALSE(task_complete);
}

}  // namespace test
}  // namespace escher
