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

};  // namespace wlan::testing
