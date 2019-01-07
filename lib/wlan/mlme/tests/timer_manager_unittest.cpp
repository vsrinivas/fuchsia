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

class MockedTimer : public Timer {
   public:
    MockedTimer() : Timer(0) {}

    zx::time Now() const override { return clock.Now(); }

    zx_status_t SetTimerImpl(zx::time d) override {
        canceled = false;
        deadline = d;
        return next_set_timer;
    }
    zx_status_t CancelTimerImpl() override {
        canceled = true;
        return ZX_OK;
    }

    zx_status_t next_set_timer = ZX_OK;
    bool canceled = false;
    zx::time deadline = zx::time(0);
    timekeeper::TestClock clock;
};

struct TimerManagerTest : public ::testing::Test {
    TimerManagerTest()
        : timer_manager(fbl::make_unique<TimerManager>(fbl::make_unique<MockedTimer>())) {}

    MockedTimer* timer() { return static_cast<MockedTimer*>(timer_manager->timer()); }

    timekeeper::TestClock* clock() { return &timer()->clock; }

    fbl::unique_ptr<TimerManager> timer_manager;
};

TEST(TimedEvent, Event) {
    TimedEvent event(zx::time(40));
    ASSERT_EQ(event.Deadline(), zx::time(40));

    // Verify the event is not triggered.
    ASSERT_TRUE(event.IsActive());
    ASSERT_FALSE(event.Triggered(zx::time(39)));

    // Verify the event is triggered.
    ASSERT_TRUE(event.IsActive());
    ASSERT_TRUE(event.Triggered(zx::time(40)));
}

TEST(TimedEvent, CanceledEventCannotTrigger) {
    TimedEvent event(zx::time(40));
    ASSERT_TRUE(event.IsActive());

    event.Cancel();

    ASSERT_FALSE(event.IsActive());
    ASSERT_FALSE(event.Triggered(zx::time(40)));
}

TEST_F(TimerManagerTest, ScheduleWithFailure) {
    // Make timer return an error the next time it is set.
    timer()->next_set_timer = ZX_ERR_IO;

    // Schedule an event and verify it failed.
    TimedEvent event;
    auto status = timer_manager->Schedule(zx::time(40), &event);
    ASSERT_EQ(status, ZX_ERR_IO);
}

TEST_F(TimerManagerTest, UpdateTimerAfterSchedule) {
    // Schedule event and verify timer was started.
    TimedEvent event;
    clock()->Set(zx::time(0));
    auto status = timer_manager->Schedule(zx::time(40), &event);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(timer()->deadline, zx::time(40));

    // Schedule another event and verify the timer did not change.
    status = timer_manager->Schedule(zx::time(50), &event);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(timer()->deadline, zx::time(40));

    // Schedule an earlier event and verify the timer was changed.
    status = timer_manager->Schedule(zx::time(30), &event);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(timer()->deadline, zx::time(30));
}

TEST_F(TimerManagerTest, Move) {
    clock()->Set(zx::time(100));
    TimerManager mgr1(fbl::make_unique<TestTimer>(0u, clock()));
    TimerManager mgr2(std::move(mgr1));
    ASSERT_EQ(mgr2.Now(), zx::time(100));
}

TEST_F(TimerManagerTest, UpdateTimerAfterTimeoutTriggered) {
    // Schedule multiple events.
    TimedEvent event;
    clock()->Set(zx::time(0));
    timer_manager->Schedule(zx::time(40), &event);
    timer_manager->Schedule(zx::time(50), &event);
    timer_manager->Schedule(zx::time(40), &event);
    timer_manager->Schedule(zx::time(70), &event);
    timer_manager->Schedule(zx::time(60), &event);

    // Advance timer without reaching a timeout.
    clock()->Set(zx::time(39));
    timer_manager->HandleTimeout();
    ASSERT_EQ(timer()->deadline, zx::time(40));
    ASSERT_FALSE(timer()->canceled);

    // Advance timer with reaching a timeout.
    clock()->Set(zx::time(40));
    timer_manager->HandleTimeout();
    ASSERT_EQ(timer()->deadline, zx::time(50));
    ASSERT_FALSE(timer()->canceled);

    // Advance timer with reaching another timeout.
    clock()->Set(zx::time(50));
    timer_manager->HandleTimeout();
    ASSERT_EQ(timer()->deadline, zx::time(60));
    ASSERT_FALSE(timer()->canceled);

    // Advance timer with reaching the last timeout.
    clock()->Set(zx::time(70));
    timer_manager->HandleTimeout();
    ASSERT_TRUE(timer()->canceled);
}

TEST_F(TimerManagerTest, Now) {
    clock()->Set(zx::time(0));
    ASSERT_EQ(timer_manager->Now(), zx::time(0));
    ASSERT_EQ(timer_manager->HandleTimeout(), zx::time(0));

    clock()->Set(zx::time(42));
    ASSERT_EQ(timer_manager->Now(), zx::time(42));
    ASSERT_EQ(timer_manager->HandleTimeout(), zx::time(42));

    clock()->Set(zx::time(55));
    ASSERT_EQ(timer_manager->Now(), zx::time(55));
    ASSERT_EQ(timer_manager->HandleTimeout(), zx::time(55));

    clock()->Set(zx::time(0));
    ASSERT_EQ(timer_manager->Now(), zx::time(0));
    ASSERT_EQ(timer_manager->HandleTimeout(), zx::time(0));
}

TEST_F(TimerManagerTest, ForgotToHandleTimeout) {
    clock()->Set(zx::time(0));
    TimedEvent event;
    timer_manager->Schedule(zx::time(40), &event);
    event.Cancel();
    clock()->Set(zx::time(50));
    timer_manager->Schedule(zx::time(60), &event);
    ASSERT_EQ(timer()->deadline, zx::time(60));
}

struct TimerManager2Test : public ::testing::Test {
    TimerManager2Test() {
        auto timer_box = fbl::make_unique<MockedTimer>();
        timer = timer_box.get();
        timer_manager = fbl::make_unique<TimerManager2<std::string>>(std::move(timer_box));
    }

    timekeeper::TestClock* clock() { return &timer->clock; }

    MockedTimer* timer;
    fbl::unique_ptr<TimerManager2<std::string>> timer_manager;
};

TEST_F(TimerManager2Test, HandleTimeout) {
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
    EXPECT_EQ(zx::time(500), timer->deadline);
    EXPECT_EQ(1u, timer_manager->NumScheduled());
}

TEST_F(TimerManager2Test, CancelNearestEvent) {
    TimeoutId foo, bar;
    timer_manager->Schedule(zx::time(100), "foo", &foo);
    timer_manager->Schedule(zx::time(200), "bar", &bar);
    EXPECT_EQ(zx::time(100), timer->deadline);
    EXPECT_EQ(2u, timer_manager->NumScheduled());

    timer_manager->Cancel(foo);
    // We don't expect Cancel() to reset the timer. Instead, the next HandleTimeout
    // should simply ignore the canceled event.
    EXPECT_EQ(zx::time(100), timer->deadline);
    EXPECT_EQ(1u, timer_manager->NumScheduled());

    clock()->Set(zx::time(150));
    size_t num_handled = 0;
    timer_manager->HandleTimeout([&](auto now, auto event, auto id) { num_handled++; });

    EXPECT_EQ(0u, num_handled);
    EXPECT_EQ(zx::time(200), timer->deadline);
    EXPECT_EQ(1u, timer_manager->NumScheduled());
}

TEST_F(TimerManager2Test, HandleLastTimeout) {
    timer_manager->Schedule(zx::time(100), "foo", nullptr);
    EXPECT_EQ(zx::time(100), timer->deadline);
    EXPECT_EQ(1u, timer_manager->NumScheduled());

    timer->deadline = zx::time(0);
    clock()->Set(zx::time(100));
    std::vector<std::string> events;
    timer_manager->HandleTimeout([&](auto now, auto event, auto id) { events.push_back(event); });
    EXPECT_EQ(events, std::vector<std::string>({"foo"}));

    // Make sure the timer has not been reset
    EXPECT_EQ(timer->deadline, zx::time(0));
}

TEST_F(TimerManager2Test, SchedulingAtLaterTimeDoesNotResetTimer) {
    timer_manager->Schedule(zx::time(300), "foo", nullptr);
    EXPECT_EQ(zx::time(300), timer->deadline);

    timer_manager->Schedule(zx::time(400), "bar", nullptr);
    EXPECT_EQ(zx::time(300), timer->deadline);
}

TEST_F(TimerManager2Test, SchedulingAtEarlierTimeResetsTimer) {
    timer_manager->Schedule(zx::time(400), "foo", nullptr);
    EXPECT_EQ(zx::time(400), timer->deadline);

    timer_manager->Schedule(zx::time(300), "bar", nullptr);
    EXPECT_EQ(zx::time(300), timer->deadline);
}

TEST_F(TimerManager2Test, ScheduleAnotherTimeoutInCallback) {
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
    EXPECT_EQ(zx::time(300), timer->deadline);
    EXPECT_EQ(1u, timer_manager->NumScheduled());
}

TEST_F(TimerManager2Test, EventsWithSameDeadlineReportedInSchedulingOrder) {
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

TEST_F(TimerManager2Test, CancelAll) {
    TimeoutId foo, bar;
    timer_manager->Schedule(zx::time(100), "foo", &foo);
    timer_manager->Schedule(zx::time(200), "bar", &bar);
    EXPECT_FALSE(timer->canceled);
    EXPECT_EQ(zx::time(100), timer->deadline);
    EXPECT_EQ(2u, timer_manager->NumScheduled());

    timer_manager->CancelAll();
    EXPECT_TRUE(timer->canceled);
    EXPECT_EQ(0u, timer_manager->NumScheduled());
}

}  // namespace
}  // namespace wlan
