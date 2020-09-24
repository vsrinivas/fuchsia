// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"

// Verify that transmissions are sent to all stations in an environment except the originator.

namespace wlan::testing {

using ::testing::NotNull;

constexpr simulation::WlanTxInfo kDefaultTxInfo = {
    .channel = {.primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0}};
constexpr wlan_ssid_t kDefaultSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
constexpr uint16_t kDefaultAssocStatus = 42;
constexpr uint16_t kDefaultDisassocReason = 5;

void checkChannel(const wlan_channel_t& channel) {
  EXPECT_EQ(channel.primary, kDefaultTxInfo.channel.primary);
  EXPECT_EQ(channel.cbw, kDefaultTxInfo.channel.cbw);
  EXPECT_EQ(channel.secondary80, kDefaultTxInfo.channel.secondary80);
}

void checkSsid(const wlan_ssid_t& ssid) {
  EXPECT_EQ(ssid.len, kDefaultSsid.len);
  EXPECT_EQ(std::memcmp(ssid.ssid, kDefaultSsid.ssid, kDefaultSsid.len), 0);
}

class SimStation : public wlan::simulation::StationIfc {
 public:
  SimStation() {
    std::memset(mac_addr_.byte, 0, common::kMacAddrLen - 1);
    mac_addr_.byte[common::kMacAddrLen - 1] = instance_count++;
  }

  // StationIfc methods
  void Rx(std::shared_ptr<const simulation::SimFrame> frame,
          std::shared_ptr<const simulation::WlanRxInfo> info) override {
    checkChannel(info->channel);
    switch (frame->FrameType()) {
      case simulation::SimFrame::FRAME_TYPE_MGMT: {
        auto mgmt_frame = std::static_pointer_cast<const simulation::SimManagementFrame>(frame);
        RxMgmtFrame(mgmt_frame);
        break;
      }

      default:
        break;
    }
  }

  void RxMgmtFrame(std::shared_ptr<const simulation::SimManagementFrame> mgmt_frame) {
    switch (mgmt_frame->MgmtFrameType()) {
      case simulation::SimManagementFrame::FRAME_TYPE_BEACON: {
        auto beacon_frame = std::static_pointer_cast<const simulation::SimBeaconFrame>(mgmt_frame);
        std::shared_ptr<simulation::InformationElement> ssid_generic_ie =
            beacon_frame->FindIe(simulation::InformationElement::IE_TYPE_SSID);
        ASSERT_THAT(ssid_generic_ie, NotNull());
        auto ssid_ie =
            std::static_pointer_cast<simulation::SsidInformationElement>(ssid_generic_ie);
        checkSsid(ssid_ie->ssid_);
        EXPECT_EQ(beacon_frame->bssid_, kDefaultBssid);
        beacon_seen_ = true;
        break;
      }

      case simulation::SimManagementFrame::FRAME_TYPE_PROBE_REQ: {
        probe_req_seen_ = true;
        break;
      }

      case simulation::SimManagementFrame::FRAME_TYPE_PROBE_RESP: {
        auto probe_resp_frame =
            std::static_pointer_cast<const simulation::SimProbeRespFrame>(mgmt_frame);
        std::shared_ptr<simulation::InformationElement> ssid_generic_ie =
            probe_resp_frame->FindIe(simulation::InformationElement::IE_TYPE_SSID);
        ASSERT_THAT(ssid_generic_ie, NotNull());
        auto ssid_ie =
            std::static_pointer_cast<simulation::SsidInformationElement>(ssid_generic_ie);
        checkSsid(ssid_ie->ssid_);
        probe_resp_seen_ = true;
        break;
      }

      case simulation::SimManagementFrame::FRAME_TYPE_ASSOC_REQ: {
        auto assoc_req_frame =
            std::static_pointer_cast<const simulation::SimAssocReqFrame>(mgmt_frame);
        EXPECT_EQ(assoc_req_frame->bssid_, kDefaultBssid);
        assoc_req_seen_ = true;
        break;
      }

      case simulation::SimManagementFrame::FRAME_TYPE_ASSOC_RESP: {
        auto assoc_resp_frame =
            std::static_pointer_cast<const simulation::SimAssocRespFrame>(mgmt_frame);
        EXPECT_EQ(assoc_resp_frame->status_, kDefaultAssocStatus);
        assoc_resp_seen_ = true;
        break;
      }

      case simulation::SimManagementFrame::FRAME_TYPE_DISASSOC_REQ: {
        auto disassoc_req_frame =
            std::static_pointer_cast<const simulation::SimDisassocReqFrame>(mgmt_frame);
        EXPECT_EQ(disassoc_req_frame->reason_, kDefaultDisassocReason);
        disassoc_req_seen_ = true;
        break;
      }

      default:
        break;
    }
  }

  static uint8_t instance_count;
  common::MacAddr mac_addr_;
  bool beacon_seen_ = false;
  bool assoc_req_seen_ = false;
  bool assoc_resp_seen_ = false;
  bool probe_req_seen_ = false;
  bool probe_resp_seen_ = false;
  bool disassoc_req_seen_ = false;
};

uint8_t SimStation::instance_count = 0;

class RxTest : public ::testing::Test {
 public:
  RxTest();
  ~RxTest();

  simulation::Environment env_;
  std::array<SimStation, 3> stations_{};
};

RxTest::RxTest() {
  for (auto sta = stations_.begin(); sta != stations_.end(); sta++) {
    env_.AddStation(sta);
  }
}

RxTest::~RxTest() {
  for (auto sta = stations_.begin(); sta != stations_.end(); sta++) {
    env_.RemoveStation(sta);
  }
}

TEST_F(RxTest, BeaconTest) {
  simulation::SimBeaconFrame beacon_frame(kDefaultSsid, kDefaultBssid);
  env_.Tx(beacon_frame, kDefaultTxInfo, &stations_[0]);
  env_.Run();
  EXPECT_EQ(stations_[0].beacon_seen_, false);
  EXPECT_EQ(stations_[1].beacon_seen_, true);
  EXPECT_EQ(stations_[2].beacon_seen_, true);
}

TEST_F(RxTest, AssocReqTest) {
  simulation::SimAssocReqFrame assoc_req_frame(stations_[1].mac_addr_, kDefaultBssid, kDefaultSsid);
  env_.Tx(assoc_req_frame, kDefaultTxInfo, &stations_[1]);
  env_.Run();
  EXPECT_EQ(stations_[0].assoc_req_seen_, true);
  EXPECT_EQ(stations_[1].assoc_req_seen_, false);
  EXPECT_EQ(stations_[2].assoc_req_seen_, true);
}

TEST_F(RxTest, AssocRespTest) {
  simulation::SimAssocRespFrame assoc_resp_frame(stations_[2].mac_addr_, stations_[0].mac_addr_,
                                                 kDefaultAssocStatus);
  env_.Tx(assoc_resp_frame, kDefaultTxInfo, &stations_[2]);
  env_.Run();
  EXPECT_EQ(stations_[0].assoc_resp_seen_, true);
  EXPECT_EQ(stations_[1].assoc_resp_seen_, true);
  EXPECT_EQ(stations_[2].assoc_resp_seen_, false);
}

TEST_F(RxTest, ProbeReqTest) {
  simulation::SimProbeReqFrame probe_req_frame(stations_[1].mac_addr_);
  env_.Tx(probe_req_frame, kDefaultTxInfo, &stations_[1]);
  env_.Run();
  EXPECT_EQ(stations_[0].probe_req_seen_, true);
  EXPECT_EQ(stations_[1].probe_req_seen_, false);
  EXPECT_EQ(stations_[2].probe_req_seen_, true);
}

TEST_F(RxTest, ProbeRespTest) {
  simulation::SimProbeRespFrame probe_resp_frame(stations_[2].mac_addr_, stations_[0].mac_addr_,
                                                 kDefaultSsid);
  env_.Tx(probe_resp_frame, kDefaultTxInfo, &stations_[2]);
  env_.Run();
  EXPECT_EQ(stations_[0].probe_resp_seen_, true);
  EXPECT_EQ(stations_[1].probe_resp_seen_, true);
  EXPECT_EQ(stations_[2].probe_resp_seen_, false);
}

TEST_F(RxTest, DisassocReqTest) {
  simulation::SimDisassocReqFrame disassoc_req_frame(stations_[2].mac_addr_, stations_[0].mac_addr_,
                                                     kDefaultDisassocReason);
  env_.Tx(disassoc_req_frame, kDefaultTxInfo, &stations_[2]);
  env_.Run();
  EXPECT_EQ(stations_[0].disassoc_req_seen_, true);
  EXPECT_EQ(stations_[1].disassoc_req_seen_, true);
  EXPECT_EQ(stations_[2].disassoc_req_seen_, false);
}
}  // namespace wlan::testing
