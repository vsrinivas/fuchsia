// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"

// Verify that transmissions are sent to all stations in an environment except the originator.

namespace wlan::testing {

constexpr wlan_channel_t kDefaultChannel = {
    .primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_ssid_t kDefaultSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
constexpr uint16_t kDefaultAssocStatus = 42;

void checkChannel(const wlan_channel_t& channel) {
  EXPECT_EQ(channel.primary, kDefaultChannel.primary);
  EXPECT_EQ(channel.cbw, kDefaultChannel.cbw);
  EXPECT_EQ(channel.secondary80, kDefaultChannel.secondary80);
}

void checkSsid(const wlan_ssid_t& ssid) {
  EXPECT_EQ(ssid.len, kDefaultSsid.len);
  EXPECT_EQ(memcmp(ssid.ssid, kDefaultSsid.ssid, WLAN_MAX_SSID_LEN), 0);
}

class SimStation : public wlan::simulation::StationIfc {
 public:
  SimStation() {
    std::memset(mac_addr_.byte, 0, common::kMacAddrLen - 1);
    mac_addr_.byte[common::kMacAddrLen - 1] = instance_count++;
  }

  // StationIfc methods
  void Rx(void* pkt) override {}
  void RxBeacon(const wlan_channel_t& channel, const wlan_ssid_t& ssid,
                const common::MacAddr& bssid) override {
    checkChannel(channel);
    checkSsid(ssid);
    EXPECT_EQ(bssid, kDefaultBssid);
    beacon_seen_ = true;
  }

  void RxAssocReq(const wlan_channel_t& channel, const common::MacAddr& src,
                  const common::MacAddr& bssid) override {
    checkChannel(channel);
    EXPECT_EQ(bssid, kDefaultBssid);
    assoc_req_seen_ = true;
  }

  void RxAssocResp(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, uint16_t status) override {
    checkChannel(channel);
    EXPECT_EQ(status, kDefaultAssocStatus);
    assoc_resp_seen_ = true;
  }

  void RxProbeReq(const wlan_channel_t& channel, const common::MacAddr& src) override {
    checkChannel(channel);
    probe_req_seen_ = true;
  }

  void RxProbeResp(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, const wlan_ssid_t& ssid) override {
    checkChannel(channel);
    checkSsid(ssid);
    probe_resp_seen_ = true;
  }

  void ReceiveNotification(void* payload) override {}

  static uint8_t instance_count;
  common::MacAddr mac_addr_;
  bool beacon_seen_ = false;
  bool assoc_req_seen_ = false;
  bool assoc_resp_seen_ = false;
  bool probe_req_seen_ = false;
  bool probe_resp_seen_ = false;
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
  env_.TxBeacon(&stations_[0], kDefaultChannel, kDefaultSsid, kDefaultBssid);
  EXPECT_EQ(stations_[0].beacon_seen_, false);
  EXPECT_EQ(stations_[1].beacon_seen_, true);
  EXPECT_EQ(stations_[2].beacon_seen_, true);
}

TEST_F(RxTest, AssocReqTest) {
  env_.TxAssocReq(&stations_[1], kDefaultChannel, stations_[1].mac_addr_, kDefaultBssid);
  EXPECT_EQ(stations_[0].assoc_req_seen_, true);
  EXPECT_EQ(stations_[1].assoc_req_seen_, false);
  EXPECT_EQ(stations_[2].assoc_req_seen_, true);
}

TEST_F(RxTest, AssocRespTest) {
  env_.TxAssocResp(&stations_[2], kDefaultChannel, stations_[2].mac_addr_, stations_[0].mac_addr_,
                   kDefaultAssocStatus);
  EXPECT_EQ(stations_[0].assoc_resp_seen_, true);
  EXPECT_EQ(stations_[1].assoc_resp_seen_, true);
  EXPECT_EQ(stations_[2].assoc_resp_seen_, false);
}

TEST_F(RxTest, ProbeReqTest) {
  env_.TxProbeReq(&stations_[1], kDefaultChannel, stations_[1].mac_addr_);
  EXPECT_EQ(stations_[0].probe_req_seen_, true);
  EXPECT_EQ(stations_[1].probe_req_seen_, false);
  EXPECT_EQ(stations_[2].probe_req_seen_, true);
}

TEST_F(RxTest, ProbeRespTest) {
  env_.TxProbeResp(&stations_[2], kDefaultChannel, stations_[2].mac_addr_, stations_[0].mac_addr_,
                   kDefaultSsid);
  EXPECT_EQ(stations_[0].probe_resp_seen_, true);
  EXPECT_EQ(stations_[1].probe_resp_seen_, true);
  EXPECT_EQ(stations_[2].probe_resp_seen_, false);
}

}  // namespace wlan::testing
