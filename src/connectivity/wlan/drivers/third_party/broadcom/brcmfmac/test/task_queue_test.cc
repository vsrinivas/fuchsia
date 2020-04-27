// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/task_queue.h"

#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <zircon/time.h>

#include <functional>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

namespace wlan {
namespace brcmfmac {
namespace {

// Test the creation and destruction ordering of elements emplaced onto the list.
TEST(TaskQueueTest, CreationDestruction) {
  class Counter {
   public:
    explicit Counter(const char* call_message, int* destroy_count)
        : call_message_(call_message), destroy_count_(destroy_count) {}

    ~Counter() {
      if (destroy_count_ != nullptr) {
        ++(*destroy_count_);
      }
    }

    void operator()() {
      constexpr bool kCalled = true;
      EXPECT_FALSE(kCalled) << call_message_;
    }

   private:
    const char* const call_message_ = "";
    int* const destroy_count_ = nullptr;
  };

  int destroy_count = 0;

  // Creating lists should not cause element calling or destruction.
  auto q1 = std::make_unique<task_queue<Counter>>();
  EXPECT_TRUE(q1->empty());
  q1->emplace("q1:1", &destroy_count);
  EXPECT_FALSE(q1->empty());
  EXPECT_EQ(0, destroy_count);
  auto q2 = std::make_unique<task_queue<Counter>>();
  EXPECT_TRUE(q2->empty());
  q2->emplace("q2:1", &destroy_count);
  EXPECT_FALSE(q2->empty());
  EXPECT_EQ(0, destroy_count);

  // Splicing lists should not cause element calling or destruction.
  q1->splice(std::move(*q2));
  EXPECT_EQ(0, destroy_count);
  EXPECT_FALSE(q1->empty());
  EXPECT_TRUE(q2->empty());

  // Self-splicing works.
  q1->splice(std::move(*q1));

  // Destroying a list now causes element destruction, but no calling.
  q1.reset();
  EXPECT_EQ(2, destroy_count);

  // Now emplace one element into q2 and then clear it.  No elements are called.
  EXPECT_TRUE(q2->empty());
  q2->emplace("q2:2", &destroy_count);
  EXPECT_FALSE(q2->empty());
  q2->clear();
  EXPECT_TRUE(q2->empty());
  EXPECT_EQ(3, destroy_count);
}

// Test the execution order of elements emplaced onto the list.
TEST(TaskQueueTest, CallOrder) {
  class Caller {
   public:
    explicit Caller(int* value, int expected_value)
        : value_(value), expected_value_(expected_value) {}

    ~Caller() {}

    void operator()(int increment) {
      EXPECT_EQ(expected_value_, *value_);
      *value_ += increment;
    }

   private:
    int* const value_ = nullptr;
    const int expected_value_ = 0;
  };

  int value = 0;

  // Calls should occur in order on a list.
  auto q1 = std::make_unique<task_queue<Caller>>();
  q1->emplace(&value, 0);
  q1->emplace(&value, 2);
  q1->emplace(&value, 4);
  EXPECT_FALSE(q1->empty());
  EXPECT_EQ(3u, q1->try_run(2));
  EXPECT_TRUE(q1->empty());
  EXPECT_EQ(6, value);

  // Nothing else left on this list.
  EXPECT_EQ(0u, q1->try_run(1));
  EXPECT_EQ(6, value);

  // If the list is cleared, no calls should occur.
  q1->emplace(&value, 6);
  q1->emplace(&value, 6);
  q1->emplace(&value, 6);
  q1->clear();
  EXPECT_EQ(0u, q1->try_run(1));
  EXPECT_EQ(6, value);

  // Another list spliced onto this one should complete in order after existing elements, and
  // subsequent additions complete after the splice.
  value = 0;
  q1->emplace(&value, 0);
  q1->emplace(&value, 5);
  q1->emplace(&value, 10);

  auto q2 = std::make_unique<task_queue<Caller>>();
  q2->emplace(&value, 15);
  q2->emplace(&value, 20);
  q1->splice(std::move(*q2));
  EXPECT_EQ(0u, q2->try_run(5));
  EXPECT_EQ(0, value);

  q1->emplace(&value, 25);

  decltype(q1)::element_type::container_type c;
  c.emplace_back(&value, 30);
  c.emplace_back(&value, 35);
  q1->splice(std::move(c));

  EXPECT_EQ(8u, q1->run(5));
  EXPECT_EQ(40, value);
}

// Test the blocking and nonblocking behavior of queue execution.
TEST(TaskQueueTest, BlockingNonBlockingRun) {
  task_queue<std::function<void()>> queue;
  int counter = 0;

  // try_run() should not block when there are no elements in the queue.
  queue.try_run();

  // Test a blocking run of run().
  sync_completion_t sync = {};
  auto thread = std::make_unique<std::thread>([&]() {
    EXPECT_EQ(0, counter);
    sync_completion_signal(&sync);
    queue.run();
    EXPECT_EQ(1, counter);
    ++counter;
  });

  // Wait for the thread to start.  We use the sync_completion_t to wait for the thread to start,
  // and then sleep sufficiently long for the thread to enter the blocking call to run().  This
  // blocking and waiting is notably not completely guaranteed to sequence these two as we'd like,
  // but even if they are out of order, no incorrect behavior will result.
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  zx::nanosleep(zx::deadline_after(zx::msec(10)));

  EXPECT_EQ(0, counter);
  queue.emplace([&]() {
    EXPECT_EQ(0, counter);
    ++counter;
  });
  thread->join();
  thread.reset();

  // The thread will have run one increment in run(), and then a second one afterwards.
  EXPECT_EQ(2, counter);

  // Now run once with try_run().
  queue.emplace([&]() {
    EXPECT_EQ(2, counter);
    ++counter;
  });
  EXPECT_EQ(2, counter);
  queue.try_run();
  EXPECT_EQ(3, counter);
}

}  // namespace
}  // namespace brcmfmac
}  // namespace wlan
