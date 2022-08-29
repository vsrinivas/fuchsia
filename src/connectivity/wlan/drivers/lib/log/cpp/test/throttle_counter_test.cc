// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <zircon/syscalls.h>

#include <condition_variable>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>
#include <wlan/drivers/internal/throttle_counter.h>

// Set up our own zx_ticks_get for this test so we can control time
static zx_ticks_t g_current_ticks = 0;
__EXPORT zx_ticks_t zx_ticks_get() { return g_current_ticks; }

namespace {

TEST(ThrottleCounter, ZxTicksOverride) {
  // Ensure that our zx_ticks override functions work
  g_current_ticks = 0;
  ASSERT_EQ(0, zx_ticks_get());

  g_current_ticks = 42;
  ASSERT_EQ(42, zx_ticks_get());
}

TEST(ThrottleCounter, ConsumeSucceedsOnce) {
  struct throttle_counter tc = {
      .capacity = 1,
      .tokens_per_second = 1.0,
      .num_throttled_events = 0uLL,
      .last_issued_tick = INT64_MIN,
  };

  uint64_t count = 0;
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));
  ASSERT_EQ(0uLL, count);

  ASSERT_FALSE(throttle_counter_consume(&tc, &count));
  ASSERT_EQ(1uLL, count);
}

TEST(ThrottleCounter, CountIncrementsAndResets) {
  struct throttle_counter tc = {
      .capacity = 1,
      .tokens_per_second = 1.0,
      .last_issued_tick = 0,  // init to 0 here so that first consume calls fail
  };

  g_current_ticks = 0;

  uint64_t count = 0;
  for (uint64_t i = 1; i <= 100; i++) {
    ASSERT_FALSE(throttle_counter_consume(&tc, &count));
    ASSERT_EQ(i, count);
  }

  g_current_ticks = zx_ticks_per_second();

  // On successful consume, max throttle count is returned
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));
  ASSERT_EQ(100uLL, count);

  // Throttle count resets back to 1 on next failed consume
  ASSERT_FALSE(throttle_counter_consume(&tc, &count));
  ASSERT_EQ(1uLL, count);
}

TEST(ThrottleCounter, CanHoldMultipleTokens) {
  struct throttle_counter tc = {
      .capacity = 2,
      .tokens_per_second = 1.0,
      .last_issued_tick = INT64_MIN,
  };

  uint64_t count = 0;
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));
  ASSERT_EQ(0uLL, count);

  ASSERT_TRUE(throttle_counter_consume(&tc, &count));
  ASSERT_EQ(0uLL, count);

  ASSERT_FALSE(throttle_counter_consume(&tc, &count));
  ASSERT_EQ(1uLL, count);
}

TEST(ThrottleCounter, TokenGeneration) {
  struct throttle_counter tc = {
      .capacity = 1,
      .tokens_per_second = 1.0,
      .last_issued_tick = INT64_MIN,
  };

  g_current_ticks = 0;

  uint64_t count = 0;
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));
  ASSERT_EQ(0uLL, count);

  // Run out of tokens
  ASSERT_FALSE(throttle_counter_consume(&tc, &count));
  ASSERT_EQ(1uLL, count);

  // New token generated
  g_current_ticks += zx_ticks_per_second();
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));
  ASSERT_EQ(1uLL, count);
}

TEST(ThrottleCounter, TokenCapacity) {
  struct throttle_counter tc = {
      .capacity = 3,
      .tokens_per_second = 1.0,
      .last_issued_tick = INT64_MIN,
  };

  uint64_t count = 0;

  g_current_ticks = 0;

  // Consume one token, we should now be left at 2 tokens left in the bucket
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));

  // Advance time by 5 seconds, we should now be back at 3 tokens but no more
  g_current_ticks += (5 * zx_ticks_per_second());

  // Consume all three tokens.
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));

  // And further attempts should fail
  ASSERT_FALSE(throttle_counter_consume(&tc, &count));
}

TEST(ThrottleCounter, TokenGenerationRate) {
  struct throttle_counter tc = {
      .capacity = 3,
      .tokens_per_second = 5.0,
      .last_issued_tick = INT64_MIN,
  };

  uint64_t count = 0;

  // Consume initial tokens
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));

  g_current_ticks += zx_ticks_per_second() / 2;

  // Now two tokens should be available
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));

  // We're only halfway to the third token so no more than that
  ASSERT_FALSE(throttle_counter_consume(&tc, &count));
}

TEST(ThrottleCounter, TokenGenerationRateLessThanOne) {
  struct throttle_counter tc = {
      .capacity = 1,
      .tokens_per_second = 0.5,
      .last_issued_tick = INT64_MIN,
  };

  uint64_t count = 0;

  // Consume initial token
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));
  g_current_ticks += zx_ticks_per_second();
  // Advanced one second but token should still not be available
  ASSERT_FALSE(throttle_counter_consume(&tc, &count));
  g_current_ticks += zx_ticks_per_second();
  // Now there should be exactly one token available
  ASSERT_TRUE(throttle_counter_consume(&tc, &count));
  ASSERT_FALSE(throttle_counter_consume(&tc, &count));
}

TEST(ThrottleCounter, ExtendedRuntime) {
  struct throttle_counter tc = {
      .capacity = 3,
      .tokens_per_second = 1.0,
      .last_issued_tick = INT64_MIN,
  };

  uint64_t count;

  for (int i = 0; i < 10'000'000; ++i) {
    // Consume one token, we should now be left at 2 tokens left in the bucket
    ASSERT_TRUE(throttle_counter_consume(&tc, &count));

    // Advance time by 5 seconds, we should now be back at 3 tokens but no more
    g_current_ticks += (5 * zx_ticks_per_second());

    // Consume all three tokens.
    ASSERT_TRUE(throttle_counter_consume(&tc, &count));
    ASSERT_TRUE(throttle_counter_consume(&tc, &count));
    ASSERT_TRUE(throttle_counter_consume(&tc, &count));

    // And further attempts should fail
    ASSERT_FALSE(throttle_counter_consume(&tc, &count));
    g_current_ticks += (3 * zx_ticks_per_second());
  }
}

TEST(ThrottleCounter, MultipleThreads) {
  struct throttle_counter tc = {
      .capacity = 1,
      .tokens_per_second = 1.0,
      .last_issued_tick = 0,
  };

  uint64_t count;

  // to be consistent for the test loop, we should start the first one with a single failed consume.
  ASSERT_FALSE(throttle_counter_consume(&tc, &count));

  constexpr size_t total_attempts = 100;
  for (size_t i = 0; i < total_attempts; i++) {
    // Set current tick to last issued tick so that consume calls will fail.
    g_current_ticks = tc.last_issued_tick;

    constexpr size_t attempts_per_thread = 1000;

    auto fail_consume = [&]() {
      uint64_t count = 0;

      for (size_t i = 0; i < attempts_per_thread; i++) {
        ASSERT_FALSE(throttle_counter_consume(&tc, &count));
      }
    };

    // Check that the returned count incremented atomically
    std::thread fail_t1{fail_consume};
    std::thread fail_t2{fail_consume};

    fail_t1.join();
    fail_t2.join();

    // Issue a new token
    g_current_ticks += zx_ticks_per_second();
    uint64_t count1 = 0;
    uint64_t count2 = 0;

    bool res1, res2;

    // Two threads contend on same token
    std::thread pass_t1{[&]() { res1 = throttle_counter_consume(&tc, &count1); }};
    std::thread pass_t2{[&]() { res2 = throttle_counter_consume(&tc, &count2); }};

    pass_t1.join();
    pass_t2.join();

    // Check that only one thread got the token
    ASSERT_TRUE(res1 != res2);

    // Check that the returned counts are consistent
    ASSERT_TRUE(((count1 == 2 * attempts_per_thread + 1) && (count2 == 1)) ||
                ((count2 == 2 * attempts_per_thread + 1) && (count1 == 1)));
  }
}

}  // namespace
