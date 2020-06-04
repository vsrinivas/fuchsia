// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-frame.h"

namespace wlan::testing {

constexpr wlan_channel_t kDefaultChannel = {
    .primary = 20, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};

class FrameIETest : public ::testing::Test {
 public:
  FrameIETest() : CSA_IE(false, kDefaultChannel.primary, 0){};
  ~FrameIETest() = default;

  simulation::CSAInformationElement CSA_IE;

  simulation::SimBeaconFrame beacon_frame_;
  simulation::SimProbeReqFrame probe_req_frame_;
  simulation::SimProbeRespFrame probe_resp_frame_;
  simulation::SimAssocReqFrame assoc_req_frame_;
  simulation::SimAssocRespFrame assoc_resp_frame_;
  simulation::SimDisassocReqFrame disassoc_req_frame_;
  simulation::SimAuthFrame auth_frame_;
};

// Verify type functions return correct value
TEST_F(FrameIETest, TypeTest) {
  EXPECT_EQ(CSA_IE.IEType(), simulation::InformationElement::IE_TYPE_CSA);

  EXPECT_EQ(beacon_frame_.FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);
  EXPECT_EQ(beacon_frame_.MgmtFrameType(), simulation::SimManagementFrame::FRAME_TYPE_BEACON);
  EXPECT_EQ(probe_req_frame_.FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);
  EXPECT_EQ(probe_req_frame_.MgmtFrameType(), simulation::SimManagementFrame::FRAME_TYPE_PROBE_REQ);
  EXPECT_EQ(probe_resp_frame_.FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);
  EXPECT_EQ(probe_resp_frame_.MgmtFrameType(),
            simulation::SimManagementFrame::FRAME_TYPE_PROBE_RESP);
  EXPECT_EQ(assoc_req_frame_.FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);
  EXPECT_EQ(assoc_req_frame_.MgmtFrameType(), simulation::SimManagementFrame::FRAME_TYPE_ASSOC_REQ);
  EXPECT_EQ(assoc_resp_frame_.FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);
  EXPECT_EQ(assoc_resp_frame_.MgmtFrameType(),
            simulation::SimManagementFrame::FRAME_TYPE_ASSOC_RESP);
  EXPECT_EQ(disassoc_req_frame_.FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);
  EXPECT_EQ(disassoc_req_frame_.MgmtFrameType(),
            simulation::SimManagementFrame::FRAME_TYPE_DISASSOC_REQ);
  EXPECT_EQ(auth_frame_.MgmtFrameType(), simulation::SimManagementFrame::FRAME_TYPE_AUTH);
}

// Test for operations about IEs.
TEST_F(FrameIETest, IEOps) {
  beacon_frame_.AddCSAIE(kDefaultChannel, 0);
  EXPECT_EQ(beacon_frame_.IEs_.size(), (size_t)1);
  EXPECT_NE(beacon_frame_.FindIE(simulation::InformationElement::IE_TYPE_CSA), nullptr);

  // Add IE with same type again, add will fail
  beacon_frame_.AddCSAIE(kDefaultChannel, 0);
  EXPECT_EQ(beacon_frame_.IEs_.size(), (size_t)1);
  beacon_frame_.RemoveIE(simulation::InformationElement::IE_TYPE_CSA);
  EXPECT_EQ(beacon_frame_.IEs_.size(), (size_t)0);
  EXPECT_EQ(beacon_frame_.FindIE(simulation::InformationElement::IE_TYPE_CSA), nullptr);
}

TEST_F(FrameIETest, DeepCopyBeaconFrame) {
  constexpr wlan_ssid_t kDefaultSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
  const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
  const common::MacAddr kDefaultSrcAddr({0x11, 0x22, 0x33, 0x44, 0x55, 0x66});
  const common::MacAddr kDefaultDstAddr({0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc});
  zx::duration kDefaultInterval = zx::msec(50);

  simulation::SimBeaconFrame origin_beacon(kDefaultSsid, kDefaultBssid);
  origin_beacon.interval_ = kDefaultInterval;
  origin_beacon.capability_info_.set_privacy(1);
  // Set values inherited from SimManagementFrame.
  origin_beacon.src_addr_ = kDefaultSrcAddr;
  origin_beacon.dst_addr_ = kDefaultDstAddr;
  origin_beacon.AddCSAIE(kDefaultChannel, 0);

  // Call copy constructor
  simulation::SimBeaconFrame copied_beacon(origin_beacon);

  // Make sure everything is copied.
  EXPECT_EQ(copied_beacon.bssid_, kDefaultBssid);
  EXPECT_EQ(copied_beacon.src_addr_, kDefaultSrcAddr);
  EXPECT_EQ(copied_beacon.dst_addr_, kDefaultDstAddr);
  EXPECT_EQ(copied_beacon.ssid_.len, kDefaultSsid.len);
  EXPECT_EQ(memcmp(copied_beacon.ssid_.ssid, kDefaultSsid.ssid, kDefaultSsid.len), 0);
  EXPECT_EQ(copied_beacon.interval_, kDefaultInterval);
  EXPECT_EQ(copied_beacon.capability_info_.val(), origin_beacon.capability_info_.val());

  EXPECT_EQ(copied_beacon.IEs_.size(), origin_beacon.IEs_.size());
  EXPECT_EQ(copied_beacon.IEs_.size(), (size_t)1);

  auto origin_ie = *origin_beacon.IEs_.begin();
  EXPECT_EQ(origin_ie->IEType(), simulation::InformationElement::IE_TYPE_CSA);
  auto origin_csa_ie = std::static_pointer_cast<simulation::CSAInformationElement>(origin_ie);
  auto copied_ie = *copied_beacon.IEs_.begin();
  EXPECT_EQ(copied_ie->IEType(), simulation::InformationElement::IE_TYPE_CSA);
  auto copied_csa_ie = std::static_pointer_cast<simulation::CSAInformationElement>(copied_ie);

  EXPECT_EQ(origin_csa_ie->channel_switch_mode_, copied_csa_ie->channel_switch_mode_);
  EXPECT_EQ(origin_csa_ie->new_channel_number_, copied_csa_ie->new_channel_number_);
  EXPECT_EQ(origin_csa_ie->channel_switch_count_, copied_csa_ie->channel_switch_count_);
  // Make sure two pointers are pointing to different places
  EXPECT_NE(origin_csa_ie.get(), copied_csa_ie.get());
}

TEST_F(FrameIETest, DeepCopyQosDataFrame) {
  const common::MacAddr kDefaultAddr1({0x11, 0x11, 0x11, 0x11, 0x11, 0x11});
  const common::MacAddr kDefaultAddr2({0x22, 0x22, 0x22, 0x22, 0x22, 0x22});
  const common::MacAddr kDefaultAddr3({0x33, 0x33, 0x33, 0x33, 0x33, 0x33});
  const std::vector<uint8_t> kDefaultPayload = {0xaa, 0xbb};

  simulation::SimQosDataFrame qos_data_frame(false, false, kDefaultAddr1, kDefaultAddr2,
                                             kDefaultAddr3, 0, kDefaultPayload);
  simulation::SimQosDataFrame copied_qos_data_frame(qos_data_frame);

  EXPECT_EQ(copied_qos_data_frame.toDS_, false);
  EXPECT_EQ(copied_qos_data_frame.fromDS_, false);
  EXPECT_EQ(copied_qos_data_frame.addr1_, kDefaultAddr1);
  EXPECT_EQ(copied_qos_data_frame.addr2_, kDefaultAddr2);
  EXPECT_EQ(copied_qos_data_frame.addr3_, kDefaultAddr3);
  EXPECT_EQ(copied_qos_data_frame.payload_, kDefaultPayload);
}

};  // namespace wlan::testing
