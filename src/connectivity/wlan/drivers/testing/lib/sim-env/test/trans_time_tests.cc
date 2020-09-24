// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-frame.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"

// zx::time() gives us an absolute time of zero
#define ABSOLUTE_TIME(delay) (zx::time() + (delay))

namespace wlan::testing {

using ::testing::NotNull;

constexpr simulation::WlanTxInfo kDefaultTxInfo = {
    .channel = {.primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0}};
constexpr wlan_ssid_t kDefaultSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});

// This is the distance between stations we used in this test.
const int32_t kDefaultTestDis = 3;
// This is the time when the first transmission start.
constexpr zx::duration kFirstTransTime = zx::msec(50);

void checkChannel(const wlan_channel_t& channel) {
  EXPECT_EQ(channel.primary, kDefaultTxInfo.channel.primary);
  EXPECT_EQ(channel.cbw, kDefaultTxInfo.channel.cbw);
  EXPECT_EQ(channel.secondary80, kDefaultTxInfo.channel.secondary80);
}

class SimStation : public wlan::simulation::StationIfc {
 public:
  SimStation() {
    std::memset(mac_addr_.byte, 0, common::kMacAddrLen - 1);
    mac_addr_.byte[common::kMacAddrLen - 1] = instance_count++;
  }

  // StationIfc methods
  void Rx(std::shared_ptr<const simulation::SimFrame> frame,
          std::shared_ptr<const simulation::WlanRxInfo> info) override;

  void RxMgmtFrame(std::shared_ptr<const simulation::SimManagementFrame> mgmt_frame,
                   std::shared_ptr<const simulation::WlanRxInfo> info);

  static uint8_t instance_count;
  common::MacAddr mac_addr_;
  std::list<zx::time> recv_times_;
  simulation::Environment* env_;
};

uint8_t SimStation::instance_count = 0;

class TransTimeTest : public ::testing::Test, public simulation::StationIfc {
 public:
  TransTimeTest();
  ~TransTimeTest();

  void Rx(std::shared_ptr<const simulation::SimFrame> frame,
          std::shared_ptr<const simulation::WlanRxInfo> info) override {}

  simulation::Environment env_;
  std::array<SimStation, 2> stations_{};

  void ScheduleBeacon(zx::duration delay);
};

void SimStation::Rx(std::shared_ptr<const simulation::SimFrame> frame,
                    std::shared_ptr<const simulation::WlanRxInfo> info) {
  checkChannel(info->channel);
  switch (frame->FrameType()) {
    case simulation::SimFrame::FRAME_TYPE_MGMT: {
      auto mgmt_frame = std::static_pointer_cast<const simulation::SimManagementFrame>(frame);
      RxMgmtFrame(mgmt_frame, info);
      break;
    }

    default:
      break;
  }
}

void SimStation::RxMgmtFrame(std::shared_ptr<const simulation::SimManagementFrame> mgmt_frame,
                             std::shared_ptr<const simulation::WlanRxInfo> info) {
  switch (mgmt_frame->MgmtFrameType()) {
    case simulation::SimManagementFrame::FRAME_TYPE_BEACON: {
      auto beacon_frame = std::static_pointer_cast<const simulation::SimBeaconFrame>(mgmt_frame);
      std::shared_ptr<simulation::InformationElement> ssid_generic_ie =
          beacon_frame->FindIe(simulation::InformationElement::IE_TYPE_SSID);
      ASSERT_THAT(ssid_generic_ie, NotNull());
      auto ssid_ie = std::static_pointer_cast<simulation::SsidInformationElement>(ssid_generic_ie);
      EXPECT_EQ(ssid_ie->ssid_.len, kDefaultSsid.len);
      EXPECT_EQ(std::memcmp(ssid_ie->ssid_.ssid, kDefaultSsid.ssid, kDefaultSsid.len), 0);
      EXPECT_EQ(beacon_frame->bssid_, kDefaultBssid);
      recv_times_.push_back(env_->GetTime());
      break;
    }
    default:
      break;
  }
}

TransTimeTest::TransTimeTest() {
  for (auto sta = stations_.begin(); sta != stations_.end(); sta++) {
    sta->env_ = &env_;
    env_.AddStation(sta);
  }
  env_.AddStation(this);
}

TransTimeTest::~TransTimeTest() {
  for (auto sta = stations_.begin(); sta != stations_.end(); sta++) {
    env_.RemoveStation(sta);
  }
  env_.RemoveStation(this);
}

void TransTimeTest::ScheduleBeacon(zx::duration delay) {
  simulation::SimBeaconFrame beacon(kDefaultSsid, kDefaultBssid);
  auto fn = std::make_unique<std::function<void()>>();
  *fn = std::bind(&simulation::Environment::Tx, &env_, beacon, kDefaultTxInfo, this);
  env_.ScheduleNotification(std::move(fn), delay);
}

TEST_F(TransTimeTest, BasicUse) {
  // Sender is at default position (0, 0).
  // The two receivers have the same distance to sender.
  env_.MoveStation(&stations_[0], kDefaultTestDis, 0);
  env_.MoveStation(&stations_[1], 0, kDefaultTestDis);
  // Calculate the transmission time here.
  const zx::duration kTestTransTime = env_.CalcTransTime(&stations_[0], this);

  ScheduleBeacon(kFirstTransTime);

  env_.Run();

  EXPECT_EQ(stations_[0].recv_times_.size(), (size_t)1);
  EXPECT_EQ(stations_[0].recv_times_.front(), ABSOLUTE_TIME(kFirstTransTime + kTestTransTime));
  EXPECT_EQ(stations_[1].recv_times_.size(), (size_t)1);
  EXPECT_EQ(stations_[1].recv_times_.front(), ABSOLUTE_TIME(kFirstTransTime + kTestTransTime));
}

/* This test case verifies that when two frames are sent back-to-back, and the second one is sent
 * when the first one is in the air(not received by stations yet), both frames should be received
 * correctly by both stations. This is to ensure the transmission process of frame will not block
 * the sender.
 */
TEST_F(TransTimeTest, SendBeforeReceive) {
  // The first frame will be received at zx::msec(50) + zx::nsec(10)
  constexpr zx::duration kSecondTransTime = zx::msec(50) + zx::nsec(5);

  env_.MoveStation(&stations_[0], kDefaultTestDis, 0);
  env_.MoveStation(&stations_[1], 0, kDefaultTestDis);
  // Calculate the transmission time here.
  const zx::duration kDefaultTestTransTime = env_.CalcTransTime(&stations_[0], this);

  ScheduleBeacon(kFirstTransTime);
  ScheduleBeacon(kSecondTransTime);

  env_.Run();

  EXPECT_EQ(stations_[0].recv_times_.size(), (size_t)2);
  EXPECT_EQ(stations_[0].recv_times_.front(),
            ABSOLUTE_TIME(kFirstTransTime + kDefaultTestTransTime));
  stations_[0].recv_times_.pop_front();
  EXPECT_EQ(stations_[0].recv_times_.front(),
            ABSOLUTE_TIME(kSecondTransTime + kDefaultTestTransTime));

  EXPECT_EQ(stations_[1].recv_times_.size(), (size_t)2);
  EXPECT_EQ(stations_[1].recv_times_.front(),
            ABSOLUTE_TIME(kFirstTransTime + kDefaultTestTransTime));
  stations_[1].recv_times_.pop_front();
  EXPECT_EQ(stations_[1].recv_times_.front(),
            ABSOLUTE_TIME(kSecondTransTime + kDefaultTestTransTime));
}

/* This test case verifies that when a station's position is changed after receiving the first
 * frame, the transmission time of the second frame will change.
 */
TEST_F(TransTimeTest, MoveAfterReceive) {
  constexpr zx::duration kSecondTransTime = zx::msec(100);
  constexpr zx::duration kStationMoveTime = zx::msec(75);

  env_.MoveStation(&stations_[0], kDefaultTestDis, 0);
  // Calculate the transmission time here.
  const zx::duration kDefaultTestTransTime = env_.CalcTransTime(&stations_[0], this);

  ScheduleBeacon(kFirstTransTime);
  ScheduleBeacon(kSecondTransTime);

  auto fn = std::make_unique<std::function<void()>>();
  *fn = std::bind(&simulation::Environment::MoveStation, &env_, &stations_[0], 2 * kDefaultTestDis,
                  0);
  env_.ScheduleNotification(std::move(fn), kStationMoveTime);

  env_.Run();

  EXPECT_EQ(stations_[0].recv_times_.size(), (size_t)2);
  EXPECT_EQ(stations_[0].recv_times_.front(),
            ABSOLUTE_TIME(kFirstTransTime + kDefaultTestTransTime));
  stations_[0].recv_times_.pop_front();
  // As the distance become twice, the transmission time will also double.
  EXPECT_EQ(stations_[0].recv_times_.front(),
            ABSOLUTE_TIME(kSecondTransTime + kDefaultTestTransTime * 2));
}

}  // namespace wlan::testing
