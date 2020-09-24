// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <functional>
#include <list>
#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <wlan/protocol/ieee80211.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-frame.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"

// zx::time() gives us an absolute time of zero
#define ABSOLUTE_TIME(delay) (zx::time() + (delay))

namespace wlan::testing {

using ::testing::NotNull;

class BeaconTest : public ::testing::Test, public simulation::StationIfc {
 public:
  // We will use the Beacon structure to keep track of all beacons we receive
  struct Beacon {
    Beacon(zx::time time, const wlan_channel_t& channel, const wlan_ssid_t& ssid,
           const common::MacAddr& bssid)
        : time_(time), channel_(channel), ssid_(ssid), bssid_(bssid) {}

    zx::time time_;
    wlan_channel_t channel_;
    // Using 0 as null value, so make sure not including 0 as the channel to switch to
    uint8_t channel_to_switch_ = 0;
    uint8_t channel_switch_count_ = 0;
    wlan_ssid_t ssid_;
    common::MacAddr bssid_;
    bool privacy = false;
  };

  BeaconTest() : ap_(&env_) { env_.AddStation(this); }
  ~BeaconTest() { env_.RemoveStation(this); }

  // Event handlers
  void StartBeaconCallback();
  void UpdateBeaconCallback();
  void AssocCallback();
  void ChannelSwitchCallback(wlan_channel_t& channel, zx::duration& interval);
  void SetSecurityCallback(wlan::simulation::FakeAp::Security sec);
  void StopBeaconCallback();

  // Test validation routines
  void ValidateStartStopBeacons();
  void ValidateUpdateBeacons();
  void ValidateChannelSwitchBeacons();
  void ValidateOverlapChannelSwitches();
  void ValidateSetSecurity();
  void ValidateErrInjBeacon();

  void ScheduleCall(void (BeaconTest::*fn)(), zx::duration when);
  void ScheduleChannelSwitchCall(void (BeaconTest::*fn)(wlan_channel_t& channel,
                                                        zx::duration& interval),
                                 zx::duration when, const wlan_channel_t& channel,
                                 const zx::duration& interval);

  void ScheduleSetSecurityCall(void (BeaconTest::*fn)(wlan::simulation::FakeAp::Security sec),
                               zx::duration when, ieee80211_cipher_suite cipher);

  simulation::Environment env_;
  simulation::FakeAp ap_;
  std::list<Beacon> beacons_received_;
  uint32_t csa_beacon_count = 0;

 private:
  // StationIfc methods
  void Rx(std::shared_ptr<const simulation::SimFrame> frame,
          std::shared_ptr<const simulation::WlanRxInfo> info) override;
};

// When we receive a beacon, just add it to our list of received beacons for later validation
void BeaconTest::Rx(std::shared_ptr<const simulation::SimFrame> frame,
                    std::shared_ptr<const simulation::WlanRxInfo> info) {
  ASSERT_EQ(frame->FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);

  auto mgmt_frame = std::static_pointer_cast<const simulation::SimManagementFrame>(frame);
  if (mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_ASSOC_RESP ||
      mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_AUTH) {
    return;
  }
  ASSERT_EQ(mgmt_frame->MgmtFrameType(), simulation::SimManagementFrame::FRAME_TYPE_BEACON);

  auto beacon_frame = std::static_pointer_cast<const simulation::SimBeaconFrame>(mgmt_frame);
  std::shared_ptr<simulation::InformationElement> ssid_generic_ie =
      beacon_frame->FindIe(simulation::InformationElement::IE_TYPE_SSID);
  ASSERT_THAT(ssid_generic_ie, NotNull());
  auto ssid_ie = std::static_pointer_cast<simulation::SsidInformationElement>(ssid_generic_ie);
  beacons_received_.emplace_back(env_.GetTime(), info->channel, ssid_ie->ssid_,
                                 beacon_frame->bssid_);
  beacons_received_.back().privacy = beacon_frame->capability_info_.privacy();
  auto csa_generic_ie = beacon_frame->FindIe(simulation::InformationElement::IE_TYPE_CSA);
  if (csa_generic_ie != nullptr) {
    csa_beacon_count++;
    auto csa_ie = std::static_pointer_cast<simulation::CsaInformationElement>(csa_generic_ie);
    beacons_received_.back().channel_to_switch_ = csa_ie->new_channel_number_;
    beacons_received_.back().channel_switch_count_ = csa_ie->channel_switch_count_;
  }
}

// Some Useful defaults
constexpr zx::duration kStartTime = zx::msec(50);
constexpr zx::duration kEndTime = zx::sec(3);
constexpr zx::duration kBeaconPeriod = zx::msec(100);
constexpr simulation::WlanTxInfo kDefaultTxInfo = {
    .channel = {.primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0}};

constexpr wlan_ssid_t kDefaultSsid = {
    .len = 15,
    .ssid = "Fuchsia Fake AP",
};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});

constexpr wlan_ssid_t kErrInjBeaconSsid = {.len = 7, .ssid = "Changed"};

void BeaconTest::ScheduleCall(void (BeaconTest::*fn)(), zx::duration when) {
  auto cb_fn = std::make_unique<std::function<void()>>();
  *cb_fn = std::bind(fn, this);
  env_.ScheduleNotification(std::move(cb_fn), when);
}

void BeaconTest::ScheduleChannelSwitchCall(void (BeaconTest::*fn)(wlan_channel_t& channel,
                                                                  zx::duration& interval),
                                           zx::duration when, const wlan_channel_t& channel,
                                           const zx::duration& interval) {
  auto cb_fn = std::make_unique<std::function<void()>>();
  *cb_fn = std::bind(fn, this, channel, interval);
  env_.ScheduleNotification(std::move(cb_fn), when);
}

void BeaconTest::ScheduleSetSecurityCall(
    void (BeaconTest::*fn)(wlan::simulation::FakeAp::Security sec), zx::duration when,
    ieee80211_cipher_suite cipher) {
  simulation::FakeAp::Security sec = {.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                                      .cipher_suite = cipher};
  auto cb_fn = std::make_unique<std::function<void()>>();
  *cb_fn = std::bind(fn, this, sec);
  env_.ScheduleNotification(std::move(cb_fn), when);
}

/*** StartStop test ***/

// Starts a beacon, then stops that beacon. Verifies that we can start and stop beaconing
// (if it doesn't stop, the simulation will run forever and we will time out). Also, verifies
// that information in the beacon (and timing between beacons) is as expected.

void BeaconTest::StartBeaconCallback() {
  ap_.SetChannel(kDefaultTxInfo.channel);
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
    EXPECT_EQ(received_beacon.channel_.primary, kDefaultTxInfo.channel.primary);
    EXPECT_EQ(received_beacon.ssid_.len, kDefaultSsid.len);
    EXPECT_EQ(memcmp(received_beacon.ssid_.ssid, kDefaultSsid.ssid, kDefaultSsid.len), 0);
    EXPECT_EQ(received_beacon.bssid_, kDefaultBssid);
    beacons_received_.pop_front();
    next_event_time += kBeaconPeriod;
  }
  EXPECT_EQ(beacons_received_.empty(), true);
}

TEST_F(BeaconTest, StartStop) {
  ScheduleCall(&BeaconTest::StartBeaconCallback, kStartTime);
  ScheduleCall(&BeaconTest::StopBeaconCallback, kEndTime);
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
constexpr wlan_ssid_t kNewSsid = {
    .len = 5,
    .ssid = "Dumbo",
};
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
    EXPECT_EQ(received_beacon.channel_.primary, kDefaultTxInfo.channel.primary);
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
  ScheduleCall(&BeaconTest::StartBeaconCallback, kStartTime);
  ScheduleCall(&BeaconTest::UpdateBeaconCallback, kUpdateTime);
  ScheduleCall(&BeaconTest::StopBeaconCallback, kEndTime);
  env_.Run();

  EXPECT_GE(env_.GetTime(), ABSOLUTE_TIME(kEndTime));

  ValidateUpdateBeacons();
}

/*** Channel Switch test ***/

// Starts a beacon, switch the channel of AP in the middle, in this process, beacon contains CSA IE
// should only be seen once, and the channel in which AP is sending these beacon will change after
// channel switch.
constexpr zx::duration kAssocTime = zx::msec(60);
constexpr zx::duration kSwitchTime = zx::sec(1);
constexpr zx::duration kCsaBeaconInterval = zx::msec(120);
constexpr zx::duration kLongCsaBeaconInterval = zx::msec(350);
constexpr wlan_channel_t kFirstChannelSwitched = {
    .primary = 10, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
const common::MacAddr kClientMacAddr({0x11, 0x22, 0x33, 0x44, 0x55, 0x66});

// Used in OverlapTest
constexpr wlan_channel_t kSecondChannelSwitched = {
    .primary = 11, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_channel_t kThirdChannelSwitched = {
    .primary = 12, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr zx::duration kShortEndTime = zx::msec(500);
constexpr zx::duration kFirstSetChannel = zx::msec(80);
constexpr zx::duration kSecondSetChannel = zx::msec(180);
constexpr zx::duration kThirdSetChannel = zx::msec(280);

// Used in StopBeaconWhenSwitching
constexpr zx::duration kVeryShortEndTime = zx::msec(100);

void BeaconTest::AssocCallback() {
  simulation::SimAuthFrame auth_req_frame(kClientMacAddr, kDefaultBssid, 1,
                                          simulation::AUTH_TYPE_OPEN, WLAN_STATUS_CODE_SUCCESS);
  env_.Tx(auth_req_frame, kDefaultTxInfo, this);
  simulation::SimAssocReqFrame assoc_req_frame(kClientMacAddr, kDefaultBssid, kDefaultSsid);
  env_.Tx(assoc_req_frame, kDefaultTxInfo, this);
}

void BeaconTest::ChannelSwitchCallback(wlan_channel_t& channel, zx::duration& interval) {
  ap_.SetCsaBeaconInterval(interval);
  ap_.SetChannel(channel);
}

void BeaconTest::ValidateChannelSwitchBeacons() {
  // Check beacons received before channel switched
  zx::time next_event_time = ABSOLUTE_TIME(kStartTime);
  // This computation will ignore the case that kSwitchTime is equal to the time we enable beacon,
  // because in that case, the number will depends on which one is scheduled to sim_env first.
  uint64_t time_to_next_beacon =
      kBeaconPeriod.get() - ((kSwitchTime.get() - kStartTime.get()) % kBeaconPeriod.get());
  uint64_t cover = kLongCsaBeaconInterval.get() - time_to_next_beacon;
  uint8_t expect_CS_count = cover / kBeaconPeriod.get() + (cover % kBeaconPeriod.get() ? 1 : 0);

  while (next_event_time < ABSOLUTE_TIME(kSwitchTime + kLongCsaBeaconInterval)) {
    ASSERT_EQ(beacons_received_.empty(), false);
    Beacon received_beacon = beacons_received_.front();
    EXPECT_EQ(received_beacon.time_, next_event_time);
    EXPECT_EQ(received_beacon.channel_.primary, kDefaultTxInfo.channel.primary);
    EXPECT_EQ(received_beacon.ssid_.len, kDefaultSsid.len);
    EXPECT_EQ(memcmp(received_beacon.ssid_.ssid, kDefaultSsid.ssid, kDefaultSsid.len), 0);
    EXPECT_EQ(received_beacon.bssid_, kDefaultBssid);
    EXPECT_EQ(received_beacon.channel_to_switch_,
              (next_event_time > ABSOLUTE_TIME(kSwitchTime)) ? kFirstChannelSwitched.primary : 0);

    if (next_event_time > ABSOLUTE_TIME(kSwitchTime)) {
      EXPECT_EQ(received_beacon.channel_to_switch_, kFirstChannelSwitched.primary);
      EXPECT_EQ(received_beacon.channel_switch_count_, expect_CS_count);
      expect_CS_count--;
    } else {
      EXPECT_EQ(received_beacon.channel_to_switch_, 0);
      EXPECT_EQ(received_beacon.channel_switch_count_, 0);
    }
    beacons_received_.pop_front();
    next_event_time += kBeaconPeriod;
  }

  // Check beacons received after channel switched
  while (next_event_time < ABSOLUTE_TIME(kEndTime)) {
    ASSERT_EQ(beacons_received_.empty(), false);
    Beacon received_beacon = beacons_received_.front();
    EXPECT_EQ(received_beacon.time_, next_event_time);
    EXPECT_EQ(received_beacon.channel_.primary, kFirstChannelSwitched.primary);
    EXPECT_EQ(received_beacon.ssid_.len, kDefaultSsid.len);
    EXPECT_EQ(memcmp(received_beacon.ssid_.ssid, kDefaultSsid.ssid, kDefaultSsid.len), 0);
    EXPECT_EQ(received_beacon.bssid_, kDefaultBssid);
    EXPECT_EQ(received_beacon.channel_to_switch_, 0);
    EXPECT_EQ(received_beacon.channel_switch_count_, 0);
    beacons_received_.pop_front();
    next_event_time += kBeaconPeriod;
  }

  EXPECT_EQ(beacons_received_.empty(), true);
  EXPECT_EQ(csa_beacon_count, (uint32_t)3);
  EXPECT_EQ(ap_.GetChannel().primary, kFirstChannelSwitched.primary);
}

TEST_F(BeaconTest, ChannelSwitch) {
  // Associate to AP to trigger channel switch, kAssocTime is a little bit later than kStartTime to
  // ensure AP being set uo first
  ScheduleCall(&BeaconTest::AssocCallback, kAssocTime);
  ScheduleCall(&BeaconTest::StartBeaconCallback, kStartTime);
  ScheduleChannelSwitchCall(&BeaconTest::ChannelSwitchCallback, kSwitchTime, kFirstChannelSwitched,
                            kLongCsaBeaconInterval);
  ScheduleCall(&BeaconTest::StopBeaconCallback, kEndTime);
  env_.Run();

  EXPECT_GE(env_.GetTime(), ABSOLUTE_TIME(kEndTime));
  ValidateChannelSwitchBeacons();
}

// This is to verify following workflow: (first beacon)->(set channel)->(second beacon)->(set
// channel again before first set channel executed)->(third beacon)->(set channel before second set
// channel executed)->(fourth beacon->channel switch) The expected results will be:
// 1. Target channel in CSA IE of each beacon will be updated after each set channel.
// 2. The very first channel of AP will be maintained the same all the way until changing to the
// last channel.
void BeaconTest::ValidateOverlapChannelSwitches() {
  ASSERT_EQ(beacons_received_.empty(), false);
  Beacon received_beacon = beacons_received_.front();
  EXPECT_EQ(received_beacon.channel_.primary, kDefaultTxInfo.channel.primary);
  EXPECT_EQ(received_beacon.channel_to_switch_, 0);
  beacons_received_.pop_front();

  ASSERT_EQ(beacons_received_.empty(), false);
  received_beacon = beacons_received_.front();
  EXPECT_EQ(received_beacon.channel_.primary, kDefaultTxInfo.channel.primary);
  EXPECT_EQ(received_beacon.channel_to_switch_, kFirstChannelSwitched.primary);
  beacons_received_.pop_front();

  ASSERT_EQ(beacons_received_.empty(), false);
  received_beacon = beacons_received_.front();
  EXPECT_EQ(received_beacon.channel_.primary, kDefaultTxInfo.channel.primary);
  EXPECT_EQ(received_beacon.channel_to_switch_, kSecondChannelSwitched.primary);
  beacons_received_.pop_front();

  ASSERT_EQ(beacons_received_.empty(), false);
  received_beacon = beacons_received_.front();
  EXPECT_EQ(received_beacon.channel_.primary, kDefaultTxInfo.channel.primary);
  EXPECT_EQ(received_beacon.channel_to_switch_, kThirdChannelSwitched.primary);
  beacons_received_.pop_front();

  ASSERT_EQ(beacons_received_.empty(), false);
  received_beacon = beacons_received_.front();
  EXPECT_EQ(received_beacon.channel_.primary, kThirdChannelSwitched.primary);
  EXPECT_EQ(received_beacon.channel_to_switch_, 0);
  beacons_received_.pop_front();

  EXPECT_EQ(beacons_received_.empty(), true);
  EXPECT_EQ(csa_beacon_count, (uint32_t)3);
  EXPECT_EQ(ap_.GetChannel().primary, kThirdChannelSwitched.primary);
}

TEST_F(BeaconTest, OverlapTest) {
  ScheduleCall(&BeaconTest::AssocCallback, kAssocTime);
  ScheduleCall(&BeaconTest::StartBeaconCallback, kStartTime);
  ScheduleChannelSwitchCall(&BeaconTest::ChannelSwitchCallback, kFirstSetChannel,
                            kFirstChannelSwitched, kCsaBeaconInterval);
  ScheduleChannelSwitchCall(&BeaconTest::ChannelSwitchCallback, kSecondSetChannel,
                            kSecondChannelSwitched, kCsaBeaconInterval);
  ScheduleChannelSwitchCall(&BeaconTest::ChannelSwitchCallback, kThirdSetChannel,
                            kThirdChannelSwitched, kCsaBeaconInterval);
  ScheduleCall(&BeaconTest::StopBeaconCallback, kShortEndTime);
  env_.Run();

  EXPECT_GE(env_.GetTime(), ABSOLUTE_TIME(kShortEndTime));

  ValidateOverlapChannelSwitches();
}

// When AP does not enable beacon, it means CSA function is not supported as well in this case, so
// AP will not leave a period of time to notify stations connected to it.
TEST_F(BeaconTest, SwitchWithoutBeaconing) {
  ScheduleCall(&BeaconTest::AssocCallback, kAssocTime);
  ScheduleChannelSwitchCall(&BeaconTest::ChannelSwitchCallback, kFirstSetChannel,
                            kFirstChannelSwitched, kCsaBeaconInterval);
  env_.Run();
  // Channel will be set immediately, no event should be scheduled
  EXPECT_GE(env_.GetTime(), ABSOLUTE_TIME(kFirstSetChannel));
  EXPECT_EQ(ap_.GetChannel().primary, kFirstChannelSwitched.primary);
}

// When AP stop beaconing during the period of time to notify stations about channel switch, it mean
// AP suddenly stop the support for CSA and will change the channel immediately.
TEST_F(BeaconTest, StopBeaconWhenSwitching) {
  ScheduleCall(&BeaconTest::AssocCallback, kAssocTime);
  ScheduleCall(&BeaconTest::StartBeaconCallback, kStartTime);
  ScheduleChannelSwitchCall(&BeaconTest::ChannelSwitchCallback, kFirstSetChannel,
                            kFirstChannelSwitched, kCsaBeaconInterval);
  ScheduleCall(&BeaconTest::StopBeaconCallback, kVeryShortEndTime);
  env_.Run();

  // When beacon stops, everything stop immediately and channel will be updated.
  EXPECT_GE(env_.GetTime(), ABSOLUTE_TIME(kVeryShortEndTime));
  EXPECT_EQ(ap_.GetChannel().primary, kFirstChannelSwitched.primary);
}

/*** Set Security/AuthType test ***/

// Check whether privacy bit is set correctly in when AP is set to different security and
// authentication types.

const size_t kValidCount = 3;
constexpr zx::duration kSecurityEndTime = zx::msec(300);

void BeaconTest::SetSecurityCallback(wlan::simulation::FakeAp::Security sec) {
  ap_.SetSecurity(sec);
}

void BeaconTest::ValidateSetSecurity() {
  size_t count = 0;
  while (count < kValidCount) {
    ASSERT_EQ(beacons_received_.empty(), false);
    Beacon received_beacon = beacons_received_.front();
    EXPECT_EQ(received_beacon.privacy, true);
    beacons_received_.pop_front();
    count++;
  }
  EXPECT_EQ(beacons_received_.empty(), true);
}

TEST_F(BeaconTest, SetSecurity) {
  ScheduleCall(&BeaconTest::StartBeaconCallback, kStartTime);
  ScheduleSetSecurityCall(&BeaconTest::SetSecurityCallback, zx::msec(0),
                          IEEE80211_CIPHER_SUITE_WEP_40);
  ScheduleSetSecurityCall(&BeaconTest::SetSecurityCallback, zx::msec(100),
                          IEEE80211_CIPHER_SUITE_TKIP);
  ScheduleSetSecurityCall(&BeaconTest::SetSecurityCallback, zx::msec(200),
                          IEEE80211_CIPHER_SUITE_WEP_104);
  ScheduleCall(&BeaconTest::StopBeaconCallback, kSecurityEndTime);
  env_.Run();
  ValidateSetSecurity();
}

void BeaconTest::ValidateErrInjBeacon() {
  // Check that we received all the beacons we expected, with the mutated SSID.
  zx::time next_event_time = ABSOLUTE_TIME(kStartTime);
  while (next_event_time < ABSOLUTE_TIME(kEndTime)) {
    ASSERT_EQ(beacons_received_.empty(), false);
    Beacon received_beacon = beacons_received_.front();
    EXPECT_EQ(received_beacon.time_, next_event_time);
    EXPECT_EQ(received_beacon.channel_.primary, kDefaultTxInfo.channel.primary);
    EXPECT_EQ(received_beacon.ssid_.len, kErrInjBeaconSsid.len);
    EXPECT_EQ(
        std::memcmp(received_beacon.ssid_.ssid, kErrInjBeaconSsid.ssid, kErrInjBeaconSsid.len), 0);
    EXPECT_EQ(received_beacon.bssid_, kDefaultBssid);
    beacons_received_.pop_front();
    next_event_time += kBeaconPeriod;
  }
  EXPECT_EQ(beacons_received_.empty(), true);
}

TEST_F(BeaconTest, ErrInjBeacon) {
  EXPECT_FALSE(ap_.CheckIfErrInjBeaconEnabled());
  // Beacon mutator functor that changes the SSID of the beacon.
  auto mutator = [](const simulation::SimBeaconFrame& b) {
    auto mutated_beacon = b;
    mutated_beacon.AddSsidIe(kErrInjBeaconSsid);
    return mutated_beacon;
  };
  ap_.AddErrInjBeacon(mutator);
  EXPECT_TRUE(ap_.CheckIfErrInjBeaconEnabled());
  ScheduleCall(&BeaconTest::StartBeaconCallback, kStartTime);
  ScheduleCall(&BeaconTest::StopBeaconCallback, kEndTime);
  env_.Run();

  ValidateErrInjBeacon();

  EXPECT_TRUE(ap_.CheckIfErrInjBeaconEnabled());
  ap_.DelErrInjBeacon();
  EXPECT_FALSE(ap_.CheckIfErrInjBeaconEnabled());
}

}  // namespace wlan::testing
