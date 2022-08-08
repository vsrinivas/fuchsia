// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/sync/completion.h>

#include <atomic>
#include <memory>
#include <thread>

#include <wlan/drivers/timer/timer.h>
#include <zxtest/zxtest.h>

namespace {

using wlan::drivers::timer::Timer;

struct TimerInfo {
  TimerInfo(async_dispatcher_t* dispatcher, Timer::Callback callback)
      : timer(dispatcher, callback, this) {}
  Timer timer;
  sync_completion_t completion;
  std::atomic<int> counter = 0;
};

class TimerTest : public zxtest::Test {
 public:
  void SetUp() override {
    dispatcher_loop_ = std::make_unique<::async::Loop>(&kAsyncLoopConfigNeverAttachToThread);
    ASSERT_OK(dispatcher_loop_->StartThread("test-timer-worker", nullptr));
  }

 protected:
  std::unique_ptr<TimerInfo> CreateTimer(Timer::Callback callback) {
    auto info = std::make_unique<TimerInfo>(dispatcher_loop_->dispatcher(), callback);
    return info;
  }

  std::unique_ptr<async::Loop> dispatcher_loop_;
};

TEST(TimerTest, Constructible) { Timer timer(nullptr, nullptr, nullptr); }

TEST_F(TimerTest, OneShot) {
  auto callback = [](void* context) {
    auto info = static_cast<TimerInfo*>(context);
    sync_completion_signal(&info->completion);
  };

  std::unique_ptr<TimerInfo> info = CreateTimer(callback);

  zx_time_t start = zx_clock_get_monotonic();
  constexpr zx_duration_t kDelay = ZX_MSEC(5);
  ASSERT_OK(info->timer.StartOneshot(kDelay));

  // Ensure that the timer calls its callback.
  ASSERT_OK(sync_completion_wait(&info->completion, ZX_TIME_INFINITE));
  zx_time_t end = zx_clock_get_monotonic();
  // Ensure that at least the specified amount of time has passed.
  ASSERT_GE(end - start, kDelay);

  // Ensure that stopping a stopped timer works.
  ASSERT_OK(info->timer.Stop());
}

TEST_F(TimerTest, Periodic) {
  auto callback = [](void* context) {
    auto info = static_cast<TimerInfo*>(context);
    if (info->counter.fetch_add(1) == 1) {
      // Signal on the second callback, fetch_add returns the value before adding.
      sync_completion_signal(&info->completion);
    }
  };

  std::unique_ptr<TimerInfo> info = CreateTimer(callback);

  constexpr zx_duration_t kInterval = ZX_MSEC(3);

  zx_time_t start = zx_clock_get_monotonic();
  ASSERT_OK(info->timer.StartPeriodic(kInterval));
  // Ensure completion of periodic timer
  ASSERT_OK(sync_completion_wait(&info->completion, ZX_TIME_INFINITE));
  zx_time_t end = zx_clock_get_monotonic();

  ASSERT_OK(info->timer.Stop());

  // Ensure that at least two time the interval has passed.
  ASSERT_GE(end - start, 2 * kInterval);
}

TEST_F(TimerTest, StartTimerInCallback) {
  constexpr zx_duration_t kDelay = ZX_MSEC(4);

  auto callback = [](void* context) {
    auto info = static_cast<TimerInfo*>(context);
    if (info->counter.fetch_add(1) == 1) {
      // Signal when we reach the nested timer, fetch_add returns the value before adding.
      sync_completion_signal(&info->completion);
    } else {
      ASSERT_OK(info->timer.StartOneshot(kDelay * 2));
    }
  };

  std::unique_ptr<TimerInfo> info = CreateTimer(callback);

  zx_time_t start = zx_clock_get_monotonic();
  ASSERT_OK(info->timer.StartOneshot(kDelay));
  // Ensure the completion is signaled
  ASSERT_OK(sync_completion_wait(&info->completion, ZX_TIME_INFINITE));
  zx_time_t end = zx_clock_get_monotonic();

  // The nested timer waited twice as long, ensure the total wait is at least three times the delay.
  ASSERT_GE(end - start, 3 * kDelay);
}

TEST_F(TimerTest, StopTimerInCallback) {
  auto callback = [](void* context) {
    auto info = static_cast<TimerInfo*>(context);
    if (info->counter.fetch_add(1) == 1) {
      // Stop on the second time around
      ASSERT_OK(info->timer.Stop());
      sync_completion_signal(&info->completion);
    }
  };

  std::unique_ptr<TimerInfo> info = CreateTimer(callback);

  constexpr zx_duration_t interval = ZX_MSEC(2);
  zx_time_t start = zx_clock_get_monotonic();
  ASSERT_OK(info->timer.StartPeriodic(interval));
  // Ensure the completion is signaled
  ASSERT_OK(sync_completion_wait(&info->completion, ZX_TIME_INFINITE));
  zx_time_t end = zx_clock_get_monotonic();

  // The callback signaled on the second call, two intervals should have elapsed.
  ASSERT_GE(end - start, 2 * interval);

  // Wait for a significant amount of time longer than the interval and then check to make sure the
  // counter wasn't further increased. Because of scheduling this is not entirely foolproof but
  // should catch problems most of the time.
  zx_nanosleep(zx_deadline_after(50 * interval));

  // After all this time the counter should still only be two.
  ASSERT_EQ(2, info->counter.load());
}

TEST_F(TimerTest, ZeroDelay) {
  auto callback = [](void* context) {
    auto info = static_cast<TimerInfo*>(context);
    sync_completion_signal(&info->completion);
  };

  std::unique_ptr<TimerInfo> info = CreateTimer(callback);

  // Starting a timer with a delay of zero should work and trigger as soon as the thread is
  // scheduled.
  ASSERT_OK(info->timer.StartOneshot(0));
  ASSERT_OK(sync_completion_wait(&info->completion, ZX_TIME_INFINITE));
}

TEST_F(TimerTest, NegativeDelay) {
  auto callback = [](void* context) {
    auto info = static_cast<TimerInfo*>(context);
    sync_completion_signal(&info->completion);
  };

  std::unique_ptr<TimerInfo> info = CreateTimer(callback);

  // Starting a timer with a negative delay should not work.
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, info->timer.StartOneshot(-100));
}

TEST_F(TimerTest, MultiThreadedDispatcher) {
  ASSERT_OK(dispatcher_loop_->StartThread("test-timer-worker-1", nullptr));
  ASSERT_OK(dispatcher_loop_->StartThread("test-timer-worker-2", nullptr));
  ASSERT_OK(dispatcher_loop_->StartThread("test-timer-worker-3", nullptr));

  constexpr int kIterations = 50;

  auto callback = [](void* context) {
    auto info = static_cast<TimerInfo*>(context);
    if (info->counter.fetch_add(1) == kIterations) {
      sync_completion_signal(&info->completion);
    }
  };

  std::unique_ptr<TimerInfo> info = CreateTimer(callback);

  constexpr zx_duration_t kInterval = ZX_MSEC(1);
  zx_time_t start = zx_clock_get_monotonic();
  ASSERT_OK(info->timer.StartPeriodic(kInterval));

  ASSERT_OK(sync_completion_wait(&info->completion, ZX_TIME_INFINITE));
  zx_time_t end = zx_clock_get_monotonic();

  // The callback signaled on the second call, two intervals should have elapsed.
  ASSERT_GE(end - start, kIterations * kInterval);

  ASSERT_OK(info->timer.Stop());

  // The counter should have been increased sufficiently before the completion signaled.
  ASSERT_GE(info->counter.load(), kIterations);
}

TEST_F(TimerTest, StartStopFromMultipleThreads) {
  auto callback = [](void*) {};

  std::unique_ptr<TimerInfo> info = CreateTimer(callback);

  std::atomic<bool> running = true;

  auto one = [&]() {
    while (running) {
      ASSERT_OK(info->timer.Stop());
      ASSERT_OK(info->timer.StartOneshot(0));
      std::this_thread::yield();
    }
  };
  auto two = [&]() {
    while (running) {
      ASSERT_OK(info->timer.StartPeriodic(ZX_MSEC(1)));
      ASSERT_OK(info->timer.Stop());
    }
  };

  std::thread first_thread(one);
  std::thread second_thread(two);

  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
  running = false;
  first_thread.join();
  second_thread.join();
}

TEST_F(TimerTest, StartFromCallback) {
  auto callback = [](void* context) {
    auto info = static_cast<TimerInfo*>(context);
    info->timer.StartOneshot(ZX_MSEC(5));
  };

  std::unique_ptr<TimerInfo> info = CreateTimer(callback);
}

}  // anonymous namespace
