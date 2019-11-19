// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"

// zx::time() gives us an absolute time of zero
#define ABSOLUTE_TIME(delay) (zx::time() + (delay))

namespace wlan::testing {

class EventTest : public ::testing::Test, public simulation::StationIfc {
 public:
  struct EventNotification {
    void (*callback)(EventTest*, uint64_t);
    uint64_t value;
  };

  EventTest() : completed_(false) { env_.AddStation(this); }
  ~EventTest() { env_.RemoveStation(this); }

  // StationIfc methods
  void Rx(void* pkt) override {}
  void RxBeacon(const wlan_channel_t& channel, const wlan_ssid_t& ssid,
                const common::MacAddr& bssid) override {}
  void RxAssocReq(const wlan_channel_t& channel, const common::MacAddr& src,
                  const common::MacAddr& bssid) override {}
  void RxAssocResp(const wlan_channel_t& channel, const common::MacAddr& srcMac,
                   const common::MacAddr& dstMac, uint16_t status) override {}
  void RxProbeReq(const wlan_channel_t& channel, const common::MacAddr& src) override{};
  void RxProbeResp(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, const wlan_ssid_t& ssid) override {}
  void ReceiveNotification(void* payload) override {
    auto notification = static_cast<EventNotification*>(payload);
    notification->callback(this, notification->value);
  }

  simulation::Environment env_;
  bool completed_;
};

/*** Uneventful test ***/

// Verify that we can run through a simulation with no events
TEST_F(EventTest, Uneventful) {
  env_.Run();
  EXPECT_EQ(env_.GetTime(), ABSOLUTE_TIME(zx::usec(0)));
}

/*** SingleEvent test ***/

void SingleEventCallback(EventTest* et, uint64_t value) { et->completed_ = true; }

// Test with a single event
TEST_F(EventTest, SingleEvent) {
  constexpr zx::duration delay = zx::msec(100);
  EventNotification notification{.callback = SingleEventCallback};
  EXPECT_EQ(env_.ScheduleNotification(this, delay, static_cast<void*>(&notification)), ZX_OK);
  env_.Run();
  EXPECT_EQ(completed_, true);

  // Time should stop at the last event
  EXPECT_EQ(env_.GetTime(), ABSOLUTE_TIME(delay));
}

/*** PeriodicEvents test ***/

constexpr uint32_t kNumPeriodicEvents = 47;
constexpr zx::duration kEventPeriod = zx::msec(78);

// Event notification must persist across calls/triggers
EventTest::EventNotification periodic_notification = {};

static void PeriodicEventCallback(EventTest* et, uint64_t value) {
  static int32_t remaining_events = kNumPeriodicEvents;
  remaining_events--;
  if (remaining_events == 0) {
    et->completed_ = true;
  } else {
    EXPECT_GT(remaining_events, 0);
    EXPECT_EQ(
        et->env_.ScheduleNotification(et, kEventPeriod, static_cast<void*>(&periodic_notification)),
        ZX_OK);
  }
}

// Test with periodic events
TEST_F(EventTest, PeriodicEvents) {
  periodic_notification.callback = PeriodicEventCallback;
  EXPECT_EQ(
      env_.ScheduleNotification(this, kEventPeriod, static_cast<void*>(&periodic_notification)),
      ZX_OK);
  env_.Run();
  EXPECT_EQ(completed_, true);
  EXPECT_EQ(env_.GetTime(), ABSOLUTE_TIME(kEventPeriod * kNumPeriodicEvents));
}

/*** InvalidTime test ***/

// Attempt to add event before current time
TEST_F(EventTest, InvalidTime) {
  constexpr zx::duration delay = zx::msec(-100);
  EventNotification notification{.callback = SingleEventCallback};
  EXPECT_EQ(env_.ScheduleNotification(this, delay, static_cast<void*>(&notification)),
            ZX_ERR_INVALID_ARGS);
  env_.Run();
  EXPECT_EQ(completed_, false);

  // Time should not have advanced
  EXPECT_EQ(env_.GetTime(), ABSOLUTE_TIME(zx::usec(0)));
}

/*** SequentialEvents test ***/

constexpr int32_t kSequentialTestSecs = 99;

void SequentialEventCallback(EventTest* et, uint64_t value) {
  static int32_t remaining_events = kSequentialTestSecs + 1;
  // Invoked at time 0, then every second thereafter
  static uint32_t expected_time = 0;
  remaining_events--;
  if (remaining_events == 0) {
    et->completed_ = true;
  } else {
    EXPECT_GT(remaining_events, 0);
    EXPECT_EQ(et->env_.GetTime(), ABSOLUTE_TIME(zx::sec(expected_time)));
  }
  expected_time++;
}

// Verify that events can be added in any order and are processed sequentially
TEST_F(EventTest, SequentialEvents) {
  EventNotification notification{.callback = SequentialEventCallback};
  // Add the even-second events
  for (int32_t i = 0; i <= kSequentialTestSecs; i += 2) {
    zx::duration delay = zx::sec(i);
    EXPECT_EQ(env_.ScheduleNotification(this, delay, static_cast<void*>(&notification)), ZX_OK);
  }
  // Add the odd-second events
  for (int32_t i = (kSequentialTestSecs % 2) ? kSequentialTestSecs : (kSequentialTestSecs - 1);
       i > 0; i -= 2) {
    zx::duration delay = zx::sec(i);
    EXPECT_EQ(env_.ScheduleNotification(this, delay, static_cast<void*>(&notification)), ZX_OK);
  }
  env_.Run();
  EXPECT_EQ(completed_, true);
  EXPECT_EQ(env_.GetTime(), ABSOLUTE_TIME(zx::sec(kSequentialTestSecs)));
}

/*** DuplicateEvents test ***/

constexpr uint32_t kDuplicateEventCount = 10;
constexpr zx::duration kDuplicateEventsTime = zx::msec(120);

void DuplicateTimeEventCallback(EventTest* et, uint64_t value) {
  static int64_t remaining_events = kDuplicateEventCount;
  static uint64_t expected_value = 0;
  remaining_events--;
  if (remaining_events == 0) {
    et->completed_ = true;
  } else {
    EXPECT_GT(remaining_events, 0);
    // Verify that events are processed in order
    EXPECT_EQ(expected_value, value);
  }
  expected_value++;
}

// Test multiple events at same time. Events should be processed in the order they were added.
TEST_F(EventTest, DuplicateEvents) {
  EventNotification notifications[kDuplicateEventCount];
  for (uint32_t i = 0; i < kDuplicateEventCount; i++) {
    notifications[i].callback = DuplicateTimeEventCallback;
    notifications[i].value = i;
    EXPECT_EQ(env_.ScheduleNotification(this, kDuplicateEventsTime,
                                        static_cast<void*>(&notifications[i])),
              ZX_OK);
  }
  env_.Run();
  EXPECT_EQ(completed_, true);
  EXPECT_EQ(env_.GetTime(), ABSOLUTE_TIME(kDuplicateEventsTime));
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

void NotificationReceived(EventTest* test_ptr, uint64_t value) {
  ASSERT_LE(value, kNotificationsCount);
  cancel_state.notifications_seen[value] = true;

  if (value == 1) {  // 2s into the test
    // Try to cancel an already-passed event
    EXPECT_EQ(ZX_ERR_NOT_FOUND, test_ptr->env_.CancelNotification(test_ptr, cancel_state.ids[0]));

    // Try to cancel our current event
    EXPECT_EQ(ZX_ERR_NOT_FOUND, test_ptr->env_.CancelNotification(test_ptr, cancel_state.ids[1]));

    // Try to cancel a future event with incorrect recipient
    EXPECT_EQ(ZX_ERR_NOT_FOUND, test_ptr->env_.CancelNotification(nullptr, cancel_state.ids[3]));

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
    EXPECT_EQ(ZX_ERR_NOT_FOUND, test_ptr->env_.CancelNotification(test_ptr, fake_id));

    // Cancel a future event (hopefully successfully)
    EXPECT_EQ(ZX_OK, test_ptr->env_.CancelNotification(test_ptr, cancel_state.ids[3]));
  }

  if (value == 2) {  // 3s into the test
    // Try cancelling a previously-cancelled event
    EXPECT_EQ(ZX_ERR_NOT_FOUND, test_ptr->env_.CancelNotification(test_ptr, cancel_state.ids[3]));
  }
}

TEST_F(EventTest, CancelEvents) {
  // Create and initialize notification objects
  EventNotification notifications[kNotificationsCount];
  for (uint64_t i = 0; i < kNotificationsCount; i++) {
    notifications[i].callback = NotificationReceived;
    notifications[i].value = i;
    cancel_state.notifications_seen[i] = false;
    EXPECT_EQ(ZX_OK, env_.ScheduleNotification(this, zx::sec(i + 1),
                                               static_cast<void*>(&notifications[i]),
                                               &cancel_state.ids[i]));
  }

  env_.Run();

  // Verify notification events seen (except the one at time 4, which should have been cancelled)
  for (uint64_t i = 0; i < kNotificationsCount; i++) {
    EXPECT_EQ(cancel_state.notifications_seen[i], (i == 3) ? false : true);
  }
}

}  // namespace wlan::testing
