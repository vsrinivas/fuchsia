// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-frame.h"

namespace wlan::testing {

using simulation::InformationElement;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::SizeIs;

const wlan_ssid_t kDefaultSsid{.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kDefaultBssid{0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
constexpr wlan_channel_t kDefaultChannel = {
    .primary = 20, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};

class FrameIeTest : public ::testing::Test {};

// Verify type functions return correct value
TEST_F(FrameIeTest, SsidIeType) {
  simulation::SsidInformationElement ssid_ie(kDefaultSsid);
  EXPECT_EQ(ssid_ie.IeType(), InformationElement::IE_TYPE_SSID);
}

TEST_F(FrameIeTest, CsaIeType) {
  simulation::CsaInformationElement csa_ie(false, kDefaultChannel.primary, 0);
  EXPECT_EQ(csa_ie.IeType(), InformationElement::IE_TYPE_CSA);
}

TEST_F(FrameIeTest, SimBeaconFrameType) {
  simulation::SimBeaconFrame beacon_frame;
  EXPECT_EQ(beacon_frame.FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);
  EXPECT_EQ(beacon_frame.MgmtFrameType(), simulation::SimManagementFrame::FRAME_TYPE_BEACON);
}

TEST_F(FrameIeTest, SimProbeReqFrameType) {
  simulation::SimProbeReqFrame probe_req_frame;
  EXPECT_EQ(probe_req_frame.FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);
  EXPECT_EQ(probe_req_frame.MgmtFrameType(), simulation::SimManagementFrame::FRAME_TYPE_PROBE_REQ);
}

TEST_F(FrameIeTest, SimProbeRespFrameType) {
  simulation::SimProbeRespFrame probe_resp_frame;
  EXPECT_EQ(probe_resp_frame.FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);
  EXPECT_EQ(probe_resp_frame.MgmtFrameType(),
            simulation::SimManagementFrame::FRAME_TYPE_PROBE_RESP);
}

TEST_F(FrameIeTest, SimAssocReqFrameType) {
  simulation::SimAssocReqFrame assoc_req_frame;
  EXPECT_EQ(assoc_req_frame.FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);
  EXPECT_EQ(assoc_req_frame.MgmtFrameType(), simulation::SimManagementFrame::FRAME_TYPE_ASSOC_REQ);
}

TEST_F(FrameIeTest, SimAssocRespFrameType) {
  simulation::SimAssocRespFrame assoc_resp_frame;
  EXPECT_EQ(assoc_resp_frame.FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);
  EXPECT_EQ(assoc_resp_frame.MgmtFrameType(),
            simulation::SimManagementFrame::FRAME_TYPE_ASSOC_RESP);
}

TEST_F(FrameIeTest, SimDisassocReqFrameType) {
  simulation::SimDisassocReqFrame disassoc_req_frame;
  EXPECT_EQ(disassoc_req_frame.FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);
  EXPECT_EQ(disassoc_req_frame.MgmtFrameType(),
            simulation::SimManagementFrame::FRAME_TYPE_DISASSOC_REQ);
}

TEST_F(FrameIeTest, SimAuthFrameType) {
  simulation::SimAuthFrame auth_frame;
  EXPECT_EQ(auth_frame.MgmtFrameType(), simulation::SimManagementFrame::FRAME_TYPE_AUTH);
}

TEST_F(FrameIeTest, SsidIeAddRemove) {
  simulation::SimBeaconFrame beacon_created_without_ssid;
  // This beacon frame was created without any SSID, so it should not have the SSID IE.
  EXPECT_THAT(beacon_created_without_ssid.IEs_, IsEmpty());
  EXPECT_THAT(beacon_created_without_ssid.FindIe(InformationElement::IE_TYPE_SSID), IsNull());
  beacon_created_without_ssid.AddSsidIe(kDefaultSsid);
  EXPECT_THAT(beacon_created_without_ssid.IEs_, SizeIs(1));
  auto ssid_untyped_ie = beacon_created_without_ssid.FindIe(InformationElement::IE_TYPE_SSID);
  ASSERT_THAT(ssid_untyped_ie, NotNull());
  ASSERT_EQ(ssid_untyped_ie->IeType(), InformationElement::IE_TYPE_SSID);
  auto ssid_ie = std::static_pointer_cast<simulation::SsidInformationElement>(ssid_untyped_ie);
  EXPECT_EQ(ssid_ie->ssid_.len, kDefaultSsid.len);
  EXPECT_THAT(ssid_ie->ssid_.ssid, ElementsAreArray(kDefaultSsid.ssid));

  // Add IE with same type again, add will fail.
  beacon_created_without_ssid.AddSsidIe(kDefaultSsid);
  EXPECT_THAT(beacon_created_without_ssid.IEs_, SizeIs(1));
  beacon_created_without_ssid.RemoveIe(InformationElement::IE_TYPE_SSID);
  EXPECT_THAT(beacon_created_without_ssid.IEs_, IsEmpty());
  EXPECT_THAT(beacon_created_without_ssid.FindIe(InformationElement::IE_TYPE_SSID), IsNull());

  simulation::SimBeaconFrame beacon_created_with_ssid(kDefaultSsid, kDefaultBssid);
  // SSID IE should have been added by the SimBeaconFrame constructor.
  EXPECT_THAT(beacon_created_with_ssid.IEs_, SizeIs(1));
  ssid_untyped_ie = beacon_created_with_ssid.FindIe(InformationElement::IE_TYPE_SSID);
  ASSERT_THAT(ssid_untyped_ie, NotNull());
  ASSERT_EQ(ssid_untyped_ie->IeType(), InformationElement::IE_TYPE_SSID);
  ssid_ie = std::static_pointer_cast<simulation::SsidInformationElement>(ssid_untyped_ie);
  EXPECT_EQ(ssid_ie->ssid_.len, kDefaultSsid.len);
  EXPECT_THAT(ssid_ie->ssid_.ssid, ElementsAreArray(kDefaultSsid.ssid));

  // Add IE with same type again, add will fail.
  beacon_created_with_ssid.AddSsidIe(kDefaultSsid);
  EXPECT_THAT(beacon_created_with_ssid.IEs_, SizeIs(1));
  // Make sure that SSID IE can be removed, for example when testing a malformed beacon.
  beacon_created_with_ssid.RemoveIe(InformationElement::IE_TYPE_SSID);
  EXPECT_THAT(beacon_created_with_ssid.IEs_, IsEmpty());
  EXPECT_THAT(beacon_created_with_ssid.FindIe(InformationElement::IE_TYPE_SSID), IsNull());
}

TEST_F(FrameIeTest, SsidIeToRawIe) {
  simulation::SsidInformationElement ssid_ie(kDefaultSsid);
  const auto raw_ie = ssid_ie.ToRawIe();
  // 2 bytes of overhead, and then 15 bytes of SSID.
  const uint8_t expected_size = 17;
  ASSERT_THAT(raw_ie, SizeIs(expected_size));
  std::vector<uint8_t> expected_bytes({InformationElement::IE_TYPE_SSID, kDefaultSsid.len});
  for (int i = 0; i < kDefaultSsid.len; ++i) {
    expected_bytes.push_back(kDefaultSsid.ssid[i]);
  }
  EXPECT_THAT(raw_ie, ElementsAreArray(expected_bytes));
}

TEST_F(FrameIeTest, CsaIeAddRemove) {
  simulation::SimBeaconFrame beacon_frame;
  beacon_frame.AddCsaIe(kDefaultChannel, 0);
  EXPECT_THAT(beacon_frame.IEs_, SizeIs(1));
  EXPECT_THAT(beacon_frame.FindIe(InformationElement::IE_TYPE_CSA), NotNull());

  // Add IE with same type again, add will fail.
  beacon_frame.AddCsaIe(kDefaultChannel, 0);
  EXPECT_THAT(beacon_frame.IEs_, SizeIs(1));
  beacon_frame.RemoveIe(InformationElement::IE_TYPE_CSA);
  EXPECT_THAT(beacon_frame.IEs_, IsEmpty());
  EXPECT_THAT(beacon_frame.FindIe(InformationElement::IE_TYPE_CSA), IsNull());
}

TEST_F(FrameIeTest, CsaIeToRawIe) {
  const bool switch_mode = true;
  const uint8_t new_channel = kDefaultChannel.primary;
  const uint8_t switch_count = 5;
  simulation::CsaInformationElement csa_ie(switch_mode, new_channel, switch_count);
  const auto raw_ie = csa_ie.ToRawIe();
  // 2 bytes of overhead, and then 3 bytes of data.
  const uint8_t expected_size = 5;
  ASSERT_THAT(raw_ie, SizeIs(expected_size));
  std::vector<uint8_t> expected_bytes(
      {InformationElement::IE_TYPE_CSA, 3, switch_mode ? 1 : 0, new_channel, switch_count});
  EXPECT_THAT(raw_ie, ElementsAreArray(expected_bytes));
}

// Tests that deep copy works for frames.
TEST_F(FrameIeTest, DeepCopyBeaconFrame) {
  const common::MacAddr kDefaultSrcAddr({0x11, 0x22, 0x33, 0x44, 0x55, 0x66});
  const common::MacAddr kDefaultDstAddr({0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc});
  zx::duration kDefaultInterval = zx::msec(50);

  simulation::SimBeaconFrame origin_beacon(kDefaultSsid, kDefaultBssid);
  // Verify that SSID IE is present.
  ASSERT_THAT(origin_beacon.IEs_, SizeIs(1));
  origin_beacon.interval_ = kDefaultInterval;
  origin_beacon.capability_info_.set_privacy(1);
  // Set values inherited from SimManagementFrame.
  origin_beacon.src_addr_ = kDefaultSrcAddr;
  origin_beacon.dst_addr_ = kDefaultDstAddr;
  origin_beacon.AddCsaIe(kDefaultChannel, 0);
  // Now SSID IE and CSA IE are present.
  ASSERT_THAT(origin_beacon.IEs_, SizeIs(2));

  // Call copy constructor.
  simulation::SimBeaconFrame copied_beacon(origin_beacon);

  // Make sure everything was copied.
  EXPECT_EQ(copied_beacon.bssid_, kDefaultBssid);
  EXPECT_EQ(copied_beacon.src_addr_, kDefaultSrcAddr);
  EXPECT_EQ(copied_beacon.dst_addr_, kDefaultDstAddr);
  EXPECT_EQ(copied_beacon.interval_, kDefaultInterval);
  EXPECT_EQ(copied_beacon.capability_info_.val(), origin_beacon.capability_info_.val());

  EXPECT_THAT(copied_beacon.IEs_, SizeIs(origin_beacon.IEs_.size()));
  EXPECT_THAT(copied_beacon.IEs_, SizeIs(2));

  // Deep check of IEs as well.
  auto origin_ssid_untyped_ie = origin_beacon.FindIe(InformationElement::IE_TYPE_SSID);
  ASSERT_THAT(origin_ssid_untyped_ie, NotNull());
  ASSERT_EQ(origin_ssid_untyped_ie->IeType(), InformationElement::IE_TYPE_SSID);
  auto origin_ssid_ie =
      std::static_pointer_cast<simulation::SsidInformationElement>(origin_ssid_untyped_ie);

  auto copied_ssid_untyped_ie = copied_beacon.FindIe(InformationElement::IE_TYPE_SSID);
  ASSERT_THAT(copied_ssid_untyped_ie, NotNull());
  ASSERT_EQ(copied_ssid_untyped_ie->IeType(), InformationElement::IE_TYPE_SSID);
  auto copied_ssid_ie =
      std::static_pointer_cast<simulation::SsidInformationElement>(copied_ssid_untyped_ie);

  ASSERT_EQ(origin_ssid_ie->ssid_.len, copied_ssid_ie->ssid_.len);
  // SSID IE arrays may contain undefined values at indices beyond the specified length. There's no
  // great gmock matcher for comparing arrays like this, so let's turn them into vectors and then
  // use a simple matcher.
  std::vector<uint8_t> origin_ssid(origin_ssid_ie->ssid_.len);
  for (int i = 0; i < origin_ssid_ie->ssid_.len; ++i) {
    origin_ssid.push_back(origin_ssid_ie->ssid_.ssid[i]);
  }
  std::vector<uint8_t> copied_ssid(copied_ssid_ie->ssid_.len);
  for (int i = 0; i < copied_ssid_ie->ssid_.len; ++i) {
    copied_ssid.push_back(copied_ssid_ie->ssid_.ssid[i]);
  }
  EXPECT_THAT(origin_ssid, ElementsAreArray(copied_ssid));

  // Make sure two pointers are pointing to different places.
  EXPECT_NE(origin_ssid_ie.get(), copied_ssid_ie.get());

  auto origin_csa_untyped_ie = origin_beacon.FindIe(InformationElement::IE_TYPE_CSA);
  ASSERT_THAT(origin_csa_untyped_ie, NotNull());
  ASSERT_EQ(origin_csa_untyped_ie->IeType(), InformationElement::IE_TYPE_CSA);
  auto origin_csa_ie =
      std::static_pointer_cast<simulation::CsaInformationElement>(origin_csa_untyped_ie);

  auto copied_csa_untyped_ie = copied_beacon.FindIe(InformationElement::IE_TYPE_CSA);
  ASSERT_THAT(copied_csa_untyped_ie, NotNull());
  ASSERT_EQ(copied_csa_untyped_ie->IeType(), InformationElement::IE_TYPE_CSA);
  auto copied_csa_ie =
      std::static_pointer_cast<simulation::CsaInformationElement>(copied_csa_untyped_ie);

  EXPECT_EQ(origin_csa_ie->channel_switch_mode_, copied_csa_ie->channel_switch_mode_);
  EXPECT_EQ(origin_csa_ie->new_channel_number_, copied_csa_ie->new_channel_number_);
  EXPECT_EQ(origin_csa_ie->channel_switch_count_, copied_csa_ie->channel_switch_count_);
  // Make sure two pointers are pointing to different places.
  EXPECT_NE(origin_csa_ie.get(), copied_csa_ie.get());
}

// If the SSID IE is removed, make sure it does not reappear when that beacon is copied.
TEST_F(FrameIeTest, RemovedSsidIeDoesNotAppearInDeepCopyOfBeaconFrame) {
  const common::MacAddr kDefaultSrcAddr({0x11, 0x22, 0x33, 0x44, 0x55, 0x66});
  const common::MacAddr kDefaultDstAddr({0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc});
  zx::duration kDefaultInterval = zx::msec(50);

  simulation::SimBeaconFrame origin_beacon(kDefaultSsid, kDefaultBssid);
  // Verify that SSID IE is present.
  ASSERT_THAT(origin_beacon.IEs_, SizeIs(1));
  origin_beacon.interval_ = kDefaultInterval;
  origin_beacon.capability_info_.set_privacy(1);
  // Set values inherited from SimManagementFrame.
  origin_beacon.src_addr_ = kDefaultSrcAddr;
  origin_beacon.dst_addr_ = kDefaultDstAddr;
  origin_beacon.RemoveIe(InformationElement::IE_TYPE_SSID);
  // Now no IEs are present.
  ASSERT_THAT(origin_beacon.IEs_, IsEmpty());

  // Call copy constructor.
  simulation::SimBeaconFrame copied_beacon(origin_beacon);
  EXPECT_THAT(copied_beacon.IEs_, IsEmpty());
  EXPECT_THAT(origin_beacon.FindIe(InformationElement::IE_TYPE_SSID), IsNull());
}

TEST_F(FrameIeTest, DeepCopyQosDataFrame) {
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
