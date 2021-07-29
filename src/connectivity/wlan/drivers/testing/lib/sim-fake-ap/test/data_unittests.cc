// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/ieee80211/cpp/fidl.h>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"

namespace wlan::testing {
namespace {

constexpr zx::duration kSimulatedClockDuration = zx::sec(10);

}  // namespace

constexpr simulation::WlanTxInfo kDefaultTxInfo = {
    .channel = {.primary = 9, .cbw = CHANNEL_BANDWIDTH_CBW20, .secondary80 = 0}};
constexpr cssid_t kApSsid = {.len = 15, .data = "Fuchsia Fake AP"};
const common::MacAddr kApBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
const common::MacAddr kSrcClientMacAddr({0x11, 0x22, 0x33, 0x44, 0xee, 0xff});
const common::MacAddr kDstClientMacAddr({0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54});
const std::vector<uint8_t> kSampleEthBody = {0x00, 0x45, 0x00, 0x00, 0xE3};

class DataTest : public ::testing::Test, public simulation::StationIfc {
 public:
  std::vector<uint8_t> CreateEthernetFrame(common::MacAddr dstAddr, common::MacAddr srcAddr,
                                           uint16_t ethType);

  DataTest() : ap_(&env_, kApBssid, kApSsid, kDefaultTxInfo.channel) { env_.AddStation(this); };
  void FinishAuth(common::MacAddr addr);
  void FinishAssoc(common::MacAddr addr);
  void ScheduleTx(common::MacAddr apAddr, common::MacAddr srcAddr, common::MacAddr dstAddr,
                  std::vector<uint8_t>& ethFrame, zx::duration delay);
  void Tx(common::MacAddr apAddr, common::MacAddr srcAddr, common::MacAddr dstAddr,
          std::vector<uint8_t>& ethFrame);
  simulation::Environment env_;
  simulation::FakeAp ap_;

  // Data frames seen by the environment with a destination address of kDstClientMacAddr
  std::list<simulation::SimQosDataFrame> sent_data_contents;

 private:
  // StationIfc methods
  void Rx(std::shared_ptr<const simulation::SimFrame> frame,
          std::shared_ptr<const simulation::WlanRxInfo> info) override;
};

std::vector<uint8_t> DataTest::CreateEthernetFrame(common::MacAddr dstAddr, common::MacAddr srcAddr,
                                                   uint16_t ethType) {
  std::vector<uint8_t> ethFrame;
  ethFrame.resize(14 + kSampleEthBody.size());
  memcpy(ethFrame.data(), &dstAddr, sizeof(dstAddr));
  memcpy(ethFrame.data() + common::kMacAddrLen, &srcAddr, sizeof(srcAddr));
  ethFrame.at(common::kMacAddrLen * 2) = ethType;
  ethFrame.at(common::kMacAddrLen * 2 + 1) = ethType >> 8;
  memcpy(ethFrame.data() + 14, kSampleEthBody.data(), kSampleEthBody.size());

  return ethFrame;
}

void DataTest::Rx(std::shared_ptr<const simulation::SimFrame> frame,
                  std::shared_ptr<const simulation::WlanRxInfo> info) {
  switch (frame->FrameType()) {
    case simulation::SimFrame::FRAME_TYPE_MGMT: {
      auto mgmt_frame = std::static_pointer_cast<const simulation::SimManagementFrame>(frame);
      // Ignore the authentication and assoc responses.
      if (mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_AUTH ||
          mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_ASSOC_RESP ||
          mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_DISASSOC_REQ) {
        return;
      }
      GTEST_FAIL();
      break;
    }
    case simulation::SimFrame::FRAME_TYPE_DATA: {
      auto data_frame = std::static_pointer_cast<const simulation::SimDataFrame>(frame);
      if (data_frame->toDS_ == 1 && data_frame->fromDS_ == 0) {
        // ignore the frames we send to the AP
        return;
      } else if (data_frame->toDS_ == 0 && data_frame->fromDS_ == 1) {
        if (data_frame->addr1_ == kDstClientMacAddr) {
          // Save these frames
          sent_data_contents.emplace_back(
              data_frame->toDS_, data_frame->fromDS_, data_frame->addr1_, data_frame->addr2_,
              data_frame->addr3_, data_frame->qosControl_, data_frame->payload_);
          return;
        }
      }
      // There should be no other data frames being sent
      GTEST_FAIL();
      break;
    }
    default:
      break;
  }
}

// Send a authentication request frame at the beginning to make the status for kSrcClientMacAddr is
// AUTHENTICATED in AP.
void DataTest::FinishAuth(common::MacAddr addr) {
  simulation::SimAuthFrame auth_req_frame(addr, kApBssid, 1, simulation::AUTH_TYPE_OPEN,
                                          ::fuchsia::wlan::ieee80211::StatusCode::SUCCESS);
  env_.Tx(auth_req_frame, kDefaultTxInfo, this);
}

void DataTest::FinishAssoc(common::MacAddr addr) {
  simulation::SimAssocReqFrame assoc_req_frame(addr, kApBssid, kApSsid);
  env_.Tx(assoc_req_frame, kDefaultTxInfo, this);
}

void DataTest::ScheduleTx(common::MacAddr apAddr, common::MacAddr srcAddr, common::MacAddr dstAddr,
                          std::vector<uint8_t>& ethFrame, zx::duration delay) {
  env_.ScheduleNotification(std::bind(&DataTest::Tx, this, apAddr, srcAddr, dstAddr, ethFrame),
                            delay);
}

void DataTest::Tx(common::MacAddr apAddr, common::MacAddr srcAddr, common::MacAddr dstAddr,
                  std::vector<uint8_t>& ethFrame) {
  simulation::SimQosDataFrame dataFrame(true, false, apAddr, srcAddr, dstAddr, 0, ethFrame);
  env_.Tx(dataFrame, kDefaultTxInfo, this);
}

TEST_F(DataTest, IgnoreWrongBssid) {
  // Assoc clients to send and data packet
  FinishAuth(kSrcClientMacAddr);
  FinishAssoc(kSrcClientMacAddr);
  FinishAuth(kDstClientMacAddr);
  FinishAssoc(kDstClientMacAddr);

  // Create data payload
  std::vector<uint8_t> ethFrame =
      CreateEthernetFrame(kDstClientMacAddr, kSrcClientMacAddr, htobe16(ETH_P_IP));

  // Create and send data frame but with the wrong ap
  const common::MacAddr kWrongApBssid({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
  ScheduleTx(kWrongApBssid, kSrcClientMacAddr, kDstClientMacAddr, ethFrame, zx::usec(50));

  env_.Run(kSimulatedClockDuration);

  // Verify fake ap did not deliver data frame since it could not see it
  EXPECT_EQ(sent_data_contents.size(), 0U);
}

TEST_F(DataTest, IgnoreNonClients) {
  // Assoc src client but not destination client
  FinishAuth(kSrcClientMacAddr);
  FinishAssoc(kSrcClientMacAddr);

  // Create data payload
  std::vector<uint8_t> ethFrame =
      CreateEthernetFrame(kDstClientMacAddr, kSrcClientMacAddr, htobe16(ETH_P_IP));

  // Create and send data frame
  ScheduleTx(kApBssid, kSrcClientMacAddr, kDstClientMacAddr, ethFrame, zx::usec(50));

  env_.Run(kSimulatedClockDuration);

  // Verify fake ap did not send any data frame to the environment
  EXPECT_EQ(sent_data_contents.size(), 0U);
}

TEST_F(DataTest, BasicUse) {
  // Assoc clients to send and data packet
  FinishAuth(kSrcClientMacAddr);
  FinishAssoc(kSrcClientMacAddr);
  FinishAuth(kDstClientMacAddr);
  FinishAssoc(kDstClientMacAddr);

  // Create data payload
  std::vector<uint8_t> ethFrame =
      CreateEthernetFrame(kDstClientMacAddr, kSrcClientMacAddr, htobe16(ETH_P_IP));

  // Create and send data frame
  ScheduleTx(kApBssid, kSrcClientMacAddr, kDstClientMacAddr, ethFrame, zx::usec(50));

  env_.Run(kSimulatedClockDuration);

  // Verify fake ap delivered appropriate data frame
  EXPECT_EQ(sent_data_contents.size(), 1U);
  EXPECT_EQ(sent_data_contents.front().toDS_, false);
  EXPECT_EQ(sent_data_contents.front().fromDS_, true);
  EXPECT_EQ(sent_data_contents.front().addr1_, kDstClientMacAddr);
  EXPECT_EQ(sent_data_contents.front().addr2_, kApBssid);
  EXPECT_EQ(sent_data_contents.front().addr3_, kSrcClientMacAddr);
  EXPECT_EQ(sent_data_contents.front().payload_, ethFrame);
}
}  // namespace wlan::testing
