// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <lib/timekeeper/test_clock.h>
#include <wlan/mlme/timer.h>
#include <wlan/mlme/timer_manager.h>

#include "test_timer.h"
#include "test_utils.h"

namespace wlan {
namespace {

class MockedTimerScheduler : public TimerScheduler {
 public:
  zx_status_t Schedule(Timer* timer, zx::time d) override {
    canceled = false;
    deadline = d;
    return ZX_OK;
  }
  zx_status_t Cancel(Timer* timer) override {
    canceled = true;
    return ZX_OK;
  }

  bool canceled = false;
  zx::time deadline = zx::time(0);
};

class MockedTimer : public Timer {
 public:
  MockedTimer() : Timer(&scheduler_, 0) {}
  zx::time Now() const override { return clock.Now(); }
  zx::time deadline() const { return scheduler_.deadline; }
  bool canceled() const { return scheduler_.canceled; }
  void Reset() { scheduler_.deadline = zx::time(0); }

  timekeeper::TestClock clock;
  MockedTimerScheduler scheduler_{};
};

struct TimerManagerTest : public ::testing::Test {
  TimerManagerTest() {
    auto timer_box = std::make_unique<MockedTimer>();
    timer = timer_box.get();
    timer_manager =
        std::make_unique<TimerManager<std::string>>(std::move(timer_box));
  }

  timekeeper::TestClock* clock() { return &timer->clock; }

  MockedTimer* timer;
  fbl::unique_ptr<TimerManager<std::string>> timer_manager;
};

TEST_F(TimerManagerTest, HandleTimeout) {
  TimeoutId one, two, three, four, five;
  timer_manager->Schedule(zx::time(300), "three", &three);
  timer_manager->Schedule(zx::time(100), "one", &one);
  timer_manager->Schedule(zx::time(500), "five", &five);
  timer_manager->Schedule(zx::time(200), "two", &two);
  timer_manager->Schedule(zx::time(400), "four", &four);

  EXPECT_EQ(5u, timer_manager->NumScheduled());

  timer_manager->Cancel(two);
  timer_manager->Cancel(four);
  EXPECT_EQ(3u, timer_manager->NumScheduled());

  clock()->Set(zx::time(350));

  std::vector<std::string> events;
  std::vector<TimeoutId> ids;
  timer_manager->HandleTimeout([&](auto now, auto event, auto id) {
    EXPECT_EQ(now, zx::time(350));
    events.push_back(event);
    ids.push_back(id);
  });

  // Only expect "one" and "three" to be reported since "two" has been canceled
  // and all others are scheduled at a later time
  EXPECT_EQ(events, std::vector<std::string>({"one", "three"}));
  EXPECT_EQ(ids, std::vector<TimeoutId>({one, three}));

  // Expect the timer to be set to "five" since "four" has been canceled
  EXPECT_EQ(zx::time(500), timer->deadline());
  EXPECT_EQ(1u, timer_manager->NumScheduled());
}

TEST_F(TimerManagerTest, CancelNearestEvent) {
  TimeoutId foo, bar;
  timer_manager->Schedule(zx::time(100), "foo", &foo);
  timer_manager->Schedule(zx::time(200), "bar", &bar);
  EXPECT_EQ(zx::time(100), timer->deadline());
  EXPECT_EQ(2u, timer_manager->NumScheduled());

  timer_manager->Cancel(foo);
  // We don't expect Cancel() to reset the timer. Instead, the next
  // HandleTimeout should simply ignore the canceled event.
  EXPECT_EQ(zx::time(100), timer->deadline());
  EXPECT_EQ(1u, timer_manager->NumScheduled());

  clock()->Set(zx::time(150));
  size_t num_handled = 0;
  timer_manager->HandleTimeout(
      [&](auto now, auto event, auto id) { num_handled++; });

  EXPECT_EQ(0u, num_handled);
  EXPECT_EQ(zx::time(200), timer->deadline());
  EXPECT_EQ(1u, timer_manager->NumScheduled());
}

TEST_F(TimerManagerTest, HandleLastTimeout) {
  timer_manager->Schedule(zx::time(100), "foo", nullptr);
  EXPECT_EQ(zx::time(100), timer->deadline());
  EXPECT_EQ(1u, timer_manager->NumScheduled());

  timer->Reset();
  clock()->Set(zx::time(100));
  std::vector<std::string> events;
  timer_manager->HandleTimeout(
      [&](auto now, auto event, auto id) { events.push_back(event); });
  EXPECT_EQ(events, std::vector<std::string>({"foo"}));

  // Make sure the timer has not been reset
  EXPECT_EQ(timer->deadline(), zx::time(0));
}

TEST_F(TimerManagerTest, SchedulingAtLaterTimeDoesNotResetTimer) {
  timer_manager->Schedule(zx::time(300), "foo", nullptr);
  EXPECT_EQ(zx::time(300), timer->deadline());

  timer_manager->Schedule(zx::time(400), "bar", nullptr);
  EXPECT_EQ(zx::time(300), timer->deadline());
}

TEST_F(TimerManagerTest, SchedulingAtEarlierTimeResetsTimer) {
  timer_manager->Schedule(zx::time(400), "foo", nullptr);
  EXPECT_EQ(zx::time(400), timer->deadline());

  timer_manager->Schedule(zx::time(300), "bar", nullptr);
  EXPECT_EQ(zx::time(300), timer->deadline());
}

TEST_F(TimerManagerTest, ScheduleAnotherTimeoutInCallback) {
  timer_manager->Schedule(zx::time(200), "foo", nullptr);
  clock()->Set(zx::time(200));

  std::vector<std::string> events;
  timer_manager->HandleTimeout([&](auto now, auto event, auto id) {
    EXPECT_EQ(now, zx::time(200));
    if (event == "foo") {
      timer_manager->Schedule(zx::time(100), "bar", nullptr);
    } else if (event == "bar") {
      timer_manager->Schedule(zx::time(300), "baz", nullptr);
    }
    events.push_back(event);
  });

  // Expect "bar" to be processed immediately
  EXPECT_EQ(events, std::vector<std::string>({"foo", "bar"}));

  // The timer should be set to "baz"
  EXPECT_EQ(zx::time(300), timer->deadline());
  EXPECT_EQ(1u, timer_manager->NumScheduled());
}

TEST_F(TimerManagerTest, EventsWithSameDeadlineReportedInSchedulingOrder) {
  constexpr size_t N = 20;
  TimeoutId ids[N];
  for (size_t i = 0; i < N; ++i) {
    timer_manager->Schedule(zx::time(100), "", &ids[i]);
  }
  EXPECT_EQ(N, timer_manager->NumScheduled());

  clock()->Set(zx::time(100));

  std::vector<TimeoutId> reported_ids;
  timer_manager->HandleTimeout(
      [&](auto now, auto event, auto id) { reported_ids.push_back(id); });

  EXPECT_RANGES_EQ(ids, reported_ids);
  EXPECT_EQ(0u, timer_manager->NumScheduled());
}

TEST_F(TimerManagerTest, CancelAll) {
  TimeoutId foo, bar;
  timer_manager->Schedule(zx::time(100), "foo", &foo);
  timer_manager->Schedule(zx::time(200), "bar", &bar);
  EXPECT_FALSE(timer->canceled());
  EXPECT_EQ(zx::time(100), timer->deadline());
  EXPECT_EQ(2u, timer_manager->NumScheduled());

  timer_manager->CancelAll();
  EXPECT_TRUE(timer->canceled());
  EXPECT_EQ(0u, timer_manager->NumScheduled());
}

}  // namespace
}  // namespace wlan
