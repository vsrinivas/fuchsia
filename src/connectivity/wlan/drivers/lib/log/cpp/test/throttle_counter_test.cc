// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <zircon/syscalls.h>

#include <condition_variable>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>
#include <wlan/drivers/internal/throttle_counter.h>

namespace {

// A test class that behaves like the TokenBucket that the throttle counter uses. Use this to
// be able to control the test environment for the throttle counter.
class TestTokenBucket {
 public:
  using ConsumeCall = std::function<bool()>;
  explicit TestTokenBucket(ConsumeCall&& consume_call) : consume_(consume_call) {}

  bool consume() { return consume_(); }

  void SetConsumeCall(ConsumeCall&& consume_call) { consume_ = std::move(consume_call); }

 private:
  ConsumeCall consume_;
};

// A latch contains a counter that is decreased with each arrival. A caller can arrive and wait
// until the counter has reached zero. Each caller waiting will be woken up and execution is
// continued when the counter reaches zero. Note that this functionality is available in C++20 so
// once our toolchain catches up we can replace this with std::latch. This is a very simplified
// version of the same interface.
class Latch {
 public:
  explicit Latch(ptrdiff_t counter) : counter_(counter) {}

  void arrive_and_wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    assert(counter_ > 0);
    if (--counter_ == 0) {
      condition_.notify_all();
    } else {
      condition_.wait(lock);
    }
  }

 private:
  ptrdiff_t counter_;
  std::mutex mutex_;
  std::condition_variable condition_;
};

using wlan::drivers::ThrottleCounter;

TEST(ThrottleCounter, ConsumeSucceeds) {
  auto consume = []() { return true; };
  TestTokenBucket bucket(std::move(consume));
  ThrottleCounter counter(bucket);

  uint64_t count = 0;
  ASSERT_TRUE(counter.consume(&count));
  ASSERT_EQ(0uLL, count);
}

TEST(ThrottleCounter, ConsumeFails) {
  auto consume = []() { return false; };
  TestTokenBucket bucket(std::move(consume));
  ThrottleCounter counter(bucket);

  uint64_t count = 0;
  ASSERT_FALSE(counter.consume(&count));
  ASSERT_EQ(1uLL, count);
  ASSERT_FALSE(counter.consume(&count));
  ASSERT_EQ(2uLL, count);
}

TEST(ThrottleCounter, MultipleThreads) {
  auto consume_fail = []() { return false; };
  TestTokenBucket bucket(std::move(consume_fail));
  ThrottleCounter counter(bucket);

  uint64_t count = 0;
  // Build up some failed attempts to have a non-zero counter
  counter.consume(&count);
  counter.consume(&count);

  // Create a consume call that only returns once 2 threads of execution has
  // called consume.
  Latch latch(2);
  auto consume_with_latch = [&]() {
    latch.arrive_and_wait();
    return true;
  };
  bucket.SetConsumeCall(std::move(consume_with_latch));

  // Create the 2 threads that will consume
  uint64_t t1_count = 0;
  bool t1_result = false;
  std::thread t1([&]() { t1_result = counter.consume(&t1_count); });

  uint64_t t2_count = 0;
  bool t2_result = false;
  std::thread t2([&]() { t2_result = counter.consume(&t2_count); });

  t1.join();
  t2.join();

  ASSERT_TRUE(t1_result);
  ASSERT_TRUE(t2_result);
  // Either of the calls should have received the full count of failed consumes from the start of
  // the test.
  ASSERT_TRUE((t1_count == 0 && t2_count == 2) || (t1_count == 2 && t2_count == 0));
}

}  // namespace
