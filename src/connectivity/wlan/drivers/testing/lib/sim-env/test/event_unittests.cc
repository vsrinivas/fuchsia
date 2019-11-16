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

}  // namespace wlan::testing
