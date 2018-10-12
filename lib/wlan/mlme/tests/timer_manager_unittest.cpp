// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <lib/timekeeper/test_clock.h>
#include <wlan/mlme/timer.h>
#include <wlan/mlme/timer_manager.h>

#include "test_timer.h"

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

    TimerManagerTest() : timer_manager(fbl::make_unique<TimerManager>(fbl::make_unique<MockedTimer>())) {}

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

}  // namespace
}  // namespace wlan
