// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/wlanif.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::brcmfmac {

// Some default AP and association request values
constexpr wlan_channel_t kDefaultChannel = {
    .primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_channel_t kSwitchedChannel = {
    .primary = 20, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_channel_t kSecondSwitchedChannel = {
    .primary = 30, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
const uint16_t kDefaultCSACount = 3;
constexpr wlan_ssid_t kDefaultSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});

class ChannelSwitchTest : public SimTest {
 public:
  // How long an individual test will run for. We need an end time because tests run until no more
  // events remain and so we need to stop aps from beaconing to drain the event queue.
  static constexpr zx::duration kTestDuration = zx::sec(100);

  void Init();

  // Schedule a future SetChannel event for the first AP in list
  void ScheduleChannelSwitch(const wlan_channel_t& new_channel, zx::duration when);

  // Send one fake CSA beacon using the identification consistent of default ssid and bssid.
  void SendFakeCSABeacon(wlan_channel_t& dst_channel);

 protected:
  // Number of received assoc responses.
  size_t assoc_resp_count_ = 0;

  // This is the interface we will use for our single client interface
  SimInterface client_ifc_;
  std::list<simulation::FakeAp*> aps_;
};

void ChannelSwitchTest::SendFakeCSABeacon(wlan_channel_t& dst_channel) {
  constexpr simulation::WlanTxInfo kDefaultTxInfo = {.channel = kDefaultChannel};

  simulation::SimBeaconFrame fake_csa_beacon(kDefaultSsid, kDefaultBssid);
  fake_csa_beacon.AddCsaIe(dst_channel, kDefaultCSACount);

  env_->Tx(fake_csa_beacon, kDefaultTxInfo, this);
}

// Create our device instance and hook up the callbacks
void ChannelSwitchTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_), ZX_OK);
}

// This function schedules a Setchannel() event for the first AP in AP list.
void ChannelSwitchTest::ScheduleChannelSwitch(const wlan_channel_t& new_channel,
                                              zx::duration when) {
  SCHEDULE_CALL(when, &simulation::FakeAp::SetChannel, aps_.front(), new_channel);
}

TEST_F(ChannelSwitchTest, ChannelSwitch) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&ap);

  client_ifc_.AssociateWith(ap, zx::msec(10));
  ScheduleChannelSwitch(kSwitchedChannel, zx::msec(500));
  env_->Run(kTestDuration);

  // Channel switch will only be triggered when assciated.
  EXPECT_EQ(client_ifc_.stats_.csa_indications.size(), 1U);
  EXPECT_EQ(client_ifc_.stats_.csa_indications.front().new_channel, kSwitchedChannel.primary);
}

// This test case verifies that in a single CSA beacon interval, if AP want to switch back to old
// channel, the client will simply cancel this switch.
TEST_F(ChannelSwitchTest, SwitchBackInSingleInterval) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&ap);

  client_ifc_.AssociateWith(ap, zx::msec(10));
  ScheduleChannelSwitch(kSwitchedChannel, zx::msec(500));
  ScheduleChannelSwitch(kDefaultChannel, zx::msec(550));

  env_->Run(kTestDuration);

  EXPECT_EQ(client_ifc_.stats_.csa_indications.size(), 0U);
}

// This test verifies that in a single CSA beacon interval, if sim-fake-ap change destination
// channel to change to, sim-fw only send up one channel switch event with the newest dst channel.
TEST_F(ChannelSwitchTest, ChangeDstAddressWhenSwitching) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&ap);

  client_ifc_.AssociateWith(ap, zx::msec(10));
  ScheduleChannelSwitch(kSwitchedChannel, zx::msec(500));
  ScheduleChannelSwitch(kSecondSwitchedChannel, zx::msec(550));

  env_->Run(kTestDuration);

  EXPECT_EQ(client_ifc_.stats_.csa_indications.size(), 1U);
  EXPECT_EQ(client_ifc_.stats_.csa_indications.front().new_channel, kSecondSwitchedChannel.primary);
}

// This test case verifies that two continuous channel switches will work as long as they are in two
// separate CSA beacon intervals of AP, which means when AP's channel actually changed.
TEST_F(ChannelSwitchTest, SwitchBackInDiffInterval) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&ap);

  client_ifc_.AssociateWith(ap, zx::msec(10));
  ScheduleChannelSwitch(kSwitchedChannel, zx::msec(500));
  // The default CSA beacon interval is 150 msec, so when it comes to 700 msec, it's the second time
  // CSA is triggered.
  ScheduleChannelSwitch(kDefaultChannel, zx::msec(700));

  env_->Run(kTestDuration);

  EXPECT_EQ(client_ifc_.stats_.csa_indications.size(), 2U);
  EXPECT_EQ(client_ifc_.stats_.csa_indications.front().new_channel, kSwitchedChannel.primary);
  client_ifc_.stats_.csa_indications.pop_front();
  EXPECT_EQ(client_ifc_.stats_.csa_indications.front().new_channel, kDefaultChannel.primary);
}

// This test verifies CSA beacons from APs which are not associated with client will not trigger
// channel switch event in driver.
TEST_F(ChannelSwitchTest, NotSwitchForDifferentAP) {
  constexpr wlan_ssid_t kWrongSsid = {.len = 14, .ssid = "Fuchsia Fake AP"};
  ASSERT_NE(kDefaultSsid.len, kWrongSsid.len);
  const common::MacAddr kWrongBssid({0x12, 0x34, 0x56, 0x78, 0x9b, 0xbc});
  ASSERT_NE(kDefaultBssid, kWrongBssid);

  Init();

  simulation::FakeAp wrong_ap(env_.get(), kWrongBssid, kWrongSsid, kDefaultChannel);
  wrong_ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&wrong_ap);

  simulation::FakeAp right_ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  right_ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&right_ap);

  client_ifc_.AssociateWith(right_ap, zx::msec(10));

  // This will trigger SetChannel() for first AP in AP list, which is wrong_ap.
  ScheduleChannelSwitch(kSwitchedChannel, zx::msec(500));

  env_->Run(kTestDuration);

  EXPECT_EQ(client_ifc_.stats_.csa_indications.size(), 0U);
}

// This test case verifies that when AP stop beaconing during CSA beacon interval, sim-fw will still
// change its channel. AP will change the change immediately when beacon is stopped.
TEST_F(ChannelSwitchTest, StopStillSwitch) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&ap);

  client_ifc_.AssociateWith(ap, zx::msec(10));
  ScheduleChannelSwitch(kSwitchedChannel, zx::msec(500));

  // Schedule DisableBeacon for sim-fake-ap
  SCHEDULE_CALL(zx::msec(600), &simulation::FakeAp::DisableBeacon, aps_.front());

  env_->Run(kTestDuration);

  EXPECT_EQ(client_ifc_.stats_.csa_indications.size(), 1U);
  EXPECT_EQ(client_ifc_.stats_.csa_indications.front().new_channel, kSwitchedChannel.primary);
}

// This test verifies that the CSA beacon will be ignored when its new channel is the same as
// sim-fw's current channel.
TEST_F(ChannelSwitchTest, ChannelSwitchToSameChannel) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&ap);

  client_ifc_.AssociateWith(ap, zx::msec(10));

  // SendFakeCSABeacon() is using the ssid and bssid of the AP which client is associated to.
  SCHEDULE_CALL(zx::msec(540), &ChannelSwitchTest::SendFakeCSABeacon, this, kDefaultChannel);

  env_->Run(kTestDuration);

  EXPECT_EQ(client_ifc_.stats_.csa_indications.size(), 0U);
}

// This test verifies that the CSA beacon will be ignored when client is doing a passive scan while
// associated with an AP.
TEST_F(ChannelSwitchTest, ChannelSwitchWhileScanning) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(60));
  aps_.push_back(&ap);

  client_ifc_.AssociateWith(ap, zx::msec(10));

  constexpr uint32_t kScanStartTimeMs = 20;
  SCHEDULE_CALL(zx::msec(kScanStartTimeMs), &SimInterface::StartScan, &client_ifc_, 0, false);

  constexpr uint32_t kCsaBeaconDelayMs =
      kScanStartTimeMs + (SimInterface::kDefaultPassiveScanDwellTimeMs / 2);
  SCHEDULE_CALL(zx::msec(kCsaBeaconDelayMs), &ChannelSwitchTest::SendFakeCSABeacon, this,
                kSwitchedChannel);

  env_->Run(kTestDuration);

  EXPECT_EQ(client_ifc_.stats_.csa_indications.size(), 0U);
}

}  // namespace wlan::brcmfmac
