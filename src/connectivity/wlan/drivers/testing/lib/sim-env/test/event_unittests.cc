// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/task.h>
#include <lib/async/time.h>
#include <zircon/time.h>

#include <functional>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"

// zx::time() gives us an absolute time of zero
#define ABSOLUTE_TIME(delay) (zx::time() + (delay))

namespace wlan::testing {
namespace {

constexpr zx::duration kSimulatedClockDuration = zx::sec(10);

}  // namespace

class EventTest : public ::testing::Test, public simulation::StationIfc {
 public:
  EventTest() : env_(std::make_unique<simulation::Environment>()), completed_(false) {
    env_->AddStation(this);
  }
  ~EventTest() {
    if (env_) {
      env_->RemoveStation(this);
    }
  }

  // StationIfc methods
  void Rx(std::shared_ptr<const simulation::SimFrame> frame,
          std::shared_ptr<const simulation::WlanRxInfo> info) override {}

  // Testing callbacks
  void SingleEventCallback();
  void PeriodicEventCallback();
  void SequentialEventCallback();
  void DuplicateTimeEventCallback(uint64_t value);
  void NotificationReceived(uint64_t value);

  std::unique_ptr<simulation::Environment> env_;
  bool completed_;
};

/*** Uneventful test ***/

// Verify that we can run through a simulation with no events
TEST_F(EventTest, Uneventful) {
  env_->Run(kSimulatedClockDuration);
  EXPECT_EQ(env_->GetTime(), ABSOLUTE_TIME(zx::usec(0)));
}

/*** SingleEvent test ***/

void EventTest::SingleEventCallback() { completed_ = true; }

// Test with a single event
TEST_F(EventTest, SingleEvent) {
  constexpr zx::duration delay = zx::msec(100);
  EXPECT_EQ(env_->ScheduleNotification(std::bind(&EventTest::SingleEventCallback, this), delay),
            ZX_OK);
  env_->Run(kSimulatedClockDuration);
  EXPECT_EQ(completed_, true);

  // Time should stop at the last event
  EXPECT_EQ(env_->GetTime(), ABSOLUTE_TIME(delay));
}

/*** PeriodicEvents test ***/

constexpr uint32_t kNumPeriodicEvents = 47;
constexpr zx::duration kEventPeriod = zx::msec(78);

void EventTest::PeriodicEventCallback() {
  static int32_t remaining_events = kNumPeriodicEvents;
  remaining_events--;
  if (remaining_events == 0) {
    completed_ = true;
  } else {
    EXPECT_GT(remaining_events, 0);
    EXPECT_EQ(env_->ScheduleNotification(std::bind(&EventTest::PeriodicEventCallback, this),
                                         kEventPeriod),
              ZX_OK);
  }
}

// Test with periodic events
TEST_F(EventTest, PeriodicEvents) {
  EXPECT_EQ(
      env_->ScheduleNotification(std::bind(&EventTest::PeriodicEventCallback, this), kEventPeriod),
      ZX_OK);
  env_->Run(kSimulatedClockDuration);
  EXPECT_EQ(completed_, true);
  EXPECT_EQ(env_->GetTime(), ABSOLUTE_TIME(kEventPeriod * kNumPeriodicEvents));
}

/*** InvalidTime test ***/

// Attempt to add event before current time
TEST_F(EventTest, InvalidTime) {
  constexpr zx::duration delay = zx::msec(-100);
  EXPECT_EQ(env_->ScheduleNotification(std::bind(&EventTest::SingleEventCallback, this), delay),
            ZX_ERR_INVALID_ARGS);
  env_->Run(kSimulatedClockDuration);
  EXPECT_EQ(completed_, false);

  // Time should not have advanced
  EXPECT_EQ(env_->GetTime(), ABSOLUTE_TIME(zx::usec(0)));
}

/*** SequentialEvents test ***/

constexpr int32_t kSequentialTestSecs = 99;

void EventTest::SequentialEventCallback() {
  static int32_t remaining_events = kSequentialTestSecs + 1;
  // Invoked at time 0, then every second thereafter
  static uint32_t expected_time = 0;
  remaining_events--;
  if (remaining_events == 0) {
    completed_ = true;
  } else {
    EXPECT_GT(remaining_events, 0);
    EXPECT_EQ(env_->GetTime(), ABSOLUTE_TIME(zx::sec(expected_time)));
  }
  expected_time++;
}

// Verify that events can be added in any order and are processed sequentially
TEST_F(EventTest, SequentialEvents) {
  // Add the even-second events
  for (int32_t i = 0; i <= kSequentialTestSecs; i += 2) {
    zx::duration delay = zx::sec(i);
    EXPECT_EQ(
        env_->ScheduleNotification(std::bind(&EventTest::SequentialEventCallback, this), delay),
        ZX_OK);
  }
  // Add the odd-second events
  for (int32_t i = (kSequentialTestSecs % 2) ? kSequentialTestSecs : (kSequentialTestSecs - 1);
       i > 0; i -= 2) {
    zx::duration delay = zx::sec(i);
    EXPECT_EQ(
        env_->ScheduleNotification(std::bind(&EventTest::SequentialEventCallback, this), delay),
        ZX_OK);
  }
  env_->Run(zx::sec(kSequentialTestSecs));
  EXPECT_EQ(completed_, true);
  EXPECT_EQ(env_->GetTime(), ABSOLUTE_TIME(zx::sec(kSequentialTestSecs)));
}

/*** DuplicateEvents test ***/

constexpr uint32_t kDuplicateEventCount = 10;
constexpr zx::duration kDuplicateEventsTime = zx::msec(120);

void EventTest::DuplicateTimeEventCallback(uint64_t value) {
  static int64_t remaining_events = kDuplicateEventCount;
  static uint64_t expected_value = 0;
  remaining_events--;
  if (remaining_events == 0) {
    completed_ = true;
  } else {
    EXPECT_GT(remaining_events, 0);
    // Verify that events are processed in order
    EXPECT_EQ(expected_value, value);
  }
  expected_value++;
}

// Test multiple events at same time. Events should be processed in the order they were added.
TEST_F(EventTest, DuplicateEvents) {
  for (uint32_t i = 0; i < kDuplicateEventCount; i++) {
    EXPECT_EQ(env_->ScheduleNotification(std::bind(&EventTest::DuplicateTimeEventCallback, this, i),
                                         kDuplicateEventsTime),
              ZX_OK);
  }
  env_->Run(kDuplicateEventsTime);
  EXPECT_EQ(completed_, true);
  EXPECT_EQ(env_->GetTime(), ABSOLUTE_TIME(kDuplicateEventsTime));
}

// Test cancelling events. Start with events scheduled at 1, 2, 3, 4, and 5 seconds into
// simulation.
//
// Timeline:
//   1s: Event fires, do nothing
//   2s: Cancel event at time 4s (ZX_OK)
//       Test calls to cancel with invalid args
//   3s: Test calls to cancel with invalid args
//   5s: Event fires, do nothing
constexpr size_t kNotificationsCount = 5;

static struct CancelNotificationState {
  bool notifications_seen[kNotificationsCount];
  uint64_t ids[kNotificationsCount];
} cancel_state;

void EventTest::NotificationReceived(uint64_t value) {
  ASSERT_LE(value, kNotificationsCount);
  cancel_state.notifications_seen[value] = true;

  if (value == 1) {  // 2s into the test
    // Try to cancel an already-passed event
    EXPECT_EQ(ZX_ERR_NOT_FOUND, env_->CancelNotification(cancel_state.ids[0]));

    // Try to cancel our current event
    EXPECT_EQ(ZX_ERR_NOT_FOUND, env_->CancelNotification(cancel_state.ids[1]));

    // Try to cancel a future event with incorrect ID
    uint64_t fake_id = 0x6b46616b654964;
    bool match_found;
    do {
      match_found = false;
      for (size_t i = 0; i < kNotificationsCount; i++) {
        if (fake_id == cancel_state.ids[i]) {
          match_found = true;
          fake_id++;
          break;
        }
      }
    } while (match_found);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, env_->CancelNotification(fake_id));

    // Cancel a future event (hopefully successfully)
    EXPECT_EQ(ZX_OK, env_->CancelNotification(cancel_state.ids[3]));
  }

  if (value == 2) {  // 3s into the test
    // Try cancelling a previously-cancelled event
    EXPECT_EQ(ZX_ERR_NOT_FOUND, env_->CancelNotification(cancel_state.ids[3]));
  }
}

TEST_F(EventTest, CancelEvents) {
  // Create and initialize notification objects
  for (uint64_t i = 0; i < kNotificationsCount; i++) {
    cancel_state.notifications_seen[i] = false;
    EXPECT_EQ(ZX_OK,
              env_->ScheduleNotification(std::bind(&EventTest::NotificationReceived, this, i),
                                         zx::sec(i + 1), &cancel_state.ids[i]));
  }

  env_->Run(kSimulatedClockDuration);

  // Verify notification events seen (except the one at time 4, which should have been cancelled)
  for (uint64_t i = 0; i < kNotificationsCount; i++) {
    EXPECT_EQ(cancel_state.notifications_seen[i], (i == 3) ? false : true);
  }
}

// Test the event system, used through the async dispatcher API.
TEST_F(EventTest, AsyncDispatcher) {
  // Set up a couple of tasks.
  struct Task : public async_task_t {
    explicit Task(zx_time_t deadline) {
      handler = [](async_dispatcher_t* dispatcher, async_task_t* task, zx_status_t status) {
        static_cast<Task*>(task)->status.emplace(status);
      };
      this->deadline = deadline;
    }

    std::optional<zx_status_t> status;
  };
  auto task1 = Task(ZX_HOUR(10));
  auto task2 = Task(ZX_HOUR(20));
  auto task3 = Task(ZX_HOUR(30));
  auto task4 = Task(ZX_HOUR(40));

  // Post the tasks in unsorted order, and then run the first one.
  EXPECT_EQ(ZX_OK, async_post_task(env_->GetDispatcher(), &task3));
  EXPECT_EQ(ZX_OK, async_post_task(env_->GetDispatcher(), &task1));
  EXPECT_EQ(ZX_OK, async_post_task(env_->GetDispatcher(), &task4));
  EXPECT_EQ(ZX_OK, async_post_task(env_->GetDispatcher(), &task2));
  env_->Run(zx::hour(15));

  // Cancel the second task, then run up to the third one.
  EXPECT_EQ(ZX_ERR_NOT_FOUND, async_cancel_task(env_->GetDispatcher(), &task1));
  EXPECT_EQ(ZX_OK, async_cancel_task(env_->GetDispatcher(), &task2));
  env_->Run(zx::hour(20));

  // Task 1 and task 3 both ran.
  EXPECT_TRUE(task1.status.has_value());
  EXPECT_TRUE(task3.status.has_value());
  EXPECT_EQ(ZX_OK, task1.status.value());
  EXPECT_EQ(ZX_OK, task3.status.value());

  // Task 2 was cancelled, task 4 hasn't fired yet.
  EXPECT_FALSE(task2.status.has_value());
  EXPECT_FALSE(task4.status.has_value());

  // Shut down the environment, and task 4 will get a shutdown notification.
  env_.reset();
  EXPECT_TRUE(task4.status.has_value());
  EXPECT_EQ(ZX_ERR_CANCELED, task4.status.value());
}

}  // namespace wlan::testing
