// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"

// zx::time() gives us an absolute time of zero
#define ABSOLUTE_TIME(delay) (zx::time() + (delay))

namespace wlan::testing {

class BeaconTest : public ::testing::Test, public simulation::StationIfc {
 public:
  struct EventNotification {
    void (BeaconTest::*callback)();
  };

  // We will use the Beacon structure to keep track of all beacons we receive
  struct Beacon {
    Beacon(zx::time time, const wlan_channel_t& channel, const wlan_ssid_t& ssid,
           const common::MacAddr& bssid)
        : time_(time), channel_(channel), ssid_(ssid), bssid_(bssid) {}

    zx::time time_;
    wlan_channel_t channel_;
    wlan_ssid_t ssid_;
    common::MacAddr bssid_;
  };

  BeaconTest() : ap_(&env_) { env_.AddStation(this); }
  ~BeaconTest() { env_.RemoveStation(this); }

  // Event handlers
  void StartBeaconCallback();
  void UpdateBeaconCallback();
  void StopBeaconCallback();

  // Test validation routines
  void ValidateStartStopBeacons();
  void ValidateUpdateBeacons();

  simulation::Environment env_;
  simulation::FakeAp ap_;
  std::list<Beacon> beacons_received_;

 private:
  // StationIfc methods
  void ReceiveNotification(void* payload) override;

  // No-op StationIfc methods

  void Rx(const simulation::SimFrame* frame) override;
};

// When we receive a beacon, just add it to our list of received beacons for later validation
void BeaconTest::Rx(const simulation::SimFrame* frame) {
  ASSERT_EQ(frame->FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);

  auto mgmt_frame = static_cast<const simulation::SimManagementFrame*>(frame);
  ASSERT_EQ(mgmt_frame->MgmtFrameType(), simulation::SimManagementFrame::FRAME_TYPE_BEACON);

  auto beacon_frame = static_cast<const simulation::SimBeaconFrame*>(mgmt_frame);
  beacons_received_.emplace_back(env_.GetTime(), beacon_frame->channel_, beacon_frame->ssid_,
                                 beacon_frame->bssid_);
}

void BeaconTest::ReceiveNotification(void* payload) {
  auto notification = static_cast<EventNotification*>(payload);
  auto handler = notification->callback;
  (this->*handler)();
}

// Some Useful defaults
constexpr zx::duration kStartTime = zx::msec(50);
constexpr zx::duration kEndTime = zx::sec(3);
constexpr zx::duration kBeaconPeriod = zx::msec(100);
constexpr wlan_channel_t kDefaultChannel = {
    .primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_ssid_t kDefaultSsid = {.ssid = "Fuchsia Fake AP", .len = 15};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});

/*** StartStop test ***/

// Starts a beacon, then stops that beacon. Verifies that we can start and stop beaconing
// (if it doesn't stop, the simulation will run forever and we will time out). Also, verifies
// that information in the beacon (and timing between beacons) is as expected.

void BeaconTest::StartBeaconCallback() {
  ap_.SetChannel(kDefaultChannel);
  ap_.SetSsid(kDefaultSsid);
  ap_.SetBssid(kDefaultBssid);

  ap_.EnableBeacon(kBeaconPeriod);
}

void BeaconTest::StopBeaconCallback() { ap_.DisableBeacon(); }

void BeaconTest::ValidateStartStopBeacons() {
  // Check that we received all the beacons we expected
  zx::time next_event_time = ABSOLUTE_TIME(kStartTime);
  while (next_event_time < ABSOLUTE_TIME(kEndTime)) {
    ASSERT_EQ(beacons_received_.empty(), false);
    Beacon received_beacon = beacons_received_.front();
    EXPECT_EQ(received_beacon.time_, next_event_time);
    EXPECT_EQ(received_beacon.channel_.primary, kDefaultChannel.primary);
    EXPECT_EQ(received_beacon.ssid_.len, kDefaultSsid.len);
    EXPECT_EQ(memcmp(received_beacon.ssid_.ssid, kDefaultSsid.ssid, kDefaultSsid.len), 0);
    EXPECT_EQ(received_beacon.bssid_, kDefaultBssid);
    beacons_received_.pop_front();
    next_event_time += kBeaconPeriod;
  }
  EXPECT_EQ(beacons_received_.empty(), true);
}

TEST_F(BeaconTest, StartStop) {
  EventNotification start_notification = {.callback = &BeaconTest::StartBeaconCallback};
  env_.ScheduleNotification(this, kStartTime, static_cast<void*>(&start_notification));

  EventNotification stop_notification = {.callback = &BeaconTest::StopBeaconCallback};
  env_.ScheduleNotification(this, kEndTime, static_cast<void*>(&stop_notification));

  env_.Run();

  EXPECT_GE(env_.GetTime(), ABSOLUTE_TIME(kEndTime));

  ValidateStartStopBeacons();
}

/*** Update test ***/

// Starts a beacon, then starts another beacon (which will replace the previous one), then stops
// beaconing. Verifies that none of the old beacons are emitted after the new beacon starts

constexpr zx::duration kUpdateTime = zx::sec(1);
constexpr zx::duration kNewBeaconPeriod = zx::msec(42);
constexpr wlan_channel_t kNewChannel = {
    .primary = 136, .cbw = WLAN_CHANNEL_BANDWIDTH__80, .secondary80 = 0};
constexpr wlan_ssid_t kNewSsid = {.ssid = "Dumbo", .len = 5};
const common::MacAddr new_bssid({0xcb, 0xa9, 0x87, 0x65, 0x43, 0x21});

void BeaconTest::UpdateBeaconCallback() {
  ap_.SetChannel(kNewChannel);
  ap_.SetSsid(kNewSsid);
  ap_.SetBssid(new_bssid);

  ap_.EnableBeacon(kNewBeaconPeriod);
}

void BeaconTest::ValidateUpdateBeacons() {
  // Check beacons received before the update
  zx::time next_event_time = ABSOLUTE_TIME(kStartTime);
  while (next_event_time < ABSOLUTE_TIME(kUpdateTime)) {
    ASSERT_EQ(beacons_received_.empty(), false);
    Beacon received_beacon = beacons_received_.front();
    EXPECT_EQ(received_beacon.time_, next_event_time);
    EXPECT_EQ(received_beacon.channel_.primary, kDefaultChannel.primary);
    EXPECT_EQ(received_beacon.ssid_.len, kDefaultSsid.len);
    EXPECT_EQ(memcmp(received_beacon.ssid_.ssid, kDefaultSsid.ssid, kDefaultSsid.len), 0);
    EXPECT_EQ(received_beacon.bssid_, kDefaultBssid);
    beacons_received_.pop_front();
    next_event_time += kBeaconPeriod;
  }

  // Check beacons received after the update
  next_event_time = ABSOLUTE_TIME(kUpdateTime);
  while (next_event_time < ABSOLUTE_TIME(kEndTime)) {
    ASSERT_EQ(beacons_received_.empty(), false);
    Beacon received_beacon = beacons_received_.front();
    EXPECT_EQ(received_beacon.time_, next_event_time);
    EXPECT_EQ(received_beacon.channel_.primary, kNewChannel.primary);
    EXPECT_EQ(received_beacon.ssid_.len, kNewSsid.len);
    EXPECT_EQ(memcmp(received_beacon.ssid_.ssid, kNewSsid.ssid, kNewSsid.len), 0);
    EXPECT_EQ(received_beacon.bssid_, new_bssid);
    beacons_received_.pop_front();
    next_event_time += kNewBeaconPeriod;
  }

  EXPECT_EQ(beacons_received_.empty(), true);
}

TEST_F(BeaconTest, Update) {
  EventNotification start_notification = {.callback = &BeaconTest::StartBeaconCallback};
  env_.ScheduleNotification(this, kStartTime, static_cast<void*>(&start_notification));

  EventNotification update_notification = {.callback = &BeaconTest::UpdateBeaconCallback};
  env_.ScheduleNotification(this, kUpdateTime, static_cast<void*>(&update_notification));

  EventNotification stop_notification = {.callback = &BeaconTest::StopBeaconCallback};
  env_.ScheduleNotification(this, kEndTime, static_cast<void*>(&stop_notification));

  env_.Run();

  EXPECT_GE(env_.GetTime(), ABSOLUTE_TIME(kEndTime));

  ValidateUpdateBeacons();
}

}  // namespace wlan::testing
