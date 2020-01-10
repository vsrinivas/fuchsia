// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::testing {

constexpr wlan_channel_t kDefaultChannel = {
    .primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_ssid_t kApSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kApBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
const common::MacAddr kClientMacAddr({0x11, 0x22, 0x33, 0x44, 0xee, 0xff});
const uint16_t kClientDisassocReason = 1;
const uint16_t kApDisassocReason = 2;

class AssocTest : public ::testing::Test, public simulation::StationIfc {
 public:
  AssocTest() : ap_(&env_, kApBssid, kApSsid, kDefaultChannel) { env_.AddStation(this); };
  void DisassocFromAp(const common::MacAddr& sta, uint16_t reason);
  simulation::Environment env_;
  simulation::FakeAp ap_;

  unsigned assoc_resp_count_ = 0;
  unsigned disassoc_req_count_ = 0;
  std::list<uint16_t> assoc_status_list_;
  std::list<uint16_t> disassoc_status_list_;

 private:
  // StationIfc methods
  void Rx(const simulation::SimFrame* frame) override;

  void ReceiveNotification(void* payload) override;
};

void validateChannel(const wlan_channel_t& channel) {
  EXPECT_EQ(channel.primary, kDefaultChannel.primary);
  EXPECT_EQ(channel.cbw, kDefaultChannel.cbw);
  EXPECT_EQ(channel.secondary80, kDefaultChannel.secondary80);
}

void AssocTest::DisassocFromAp(const common::MacAddr& sta, uint16_t reason) {
  EXPECT_EQ(ap_.GetNumClients(), 1U);
  ap_.DisassocSta(sta, reason);
}

void AssocTest::Rx(const simulation::SimFrame* frame) {
  ASSERT_EQ(frame->FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);

  auto mgmt_frame = static_cast<const simulation::SimManagementFrame*>(frame);

  if (mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_ASSOC_RESP) {
    auto assoc_resp_frame = static_cast<const simulation::SimAssocRespFrame*>(mgmt_frame);
    validateChannel(assoc_resp_frame->channel_);
    EXPECT_EQ(assoc_resp_frame->src_addr_, kApBssid);
    EXPECT_EQ(assoc_resp_frame->dst_addr_, kClientMacAddr);

    assoc_resp_count_++;
    assoc_status_list_.push_back(assoc_resp_frame->status_);
  } else if (mgmt_frame->MgmtFrameType() ==
             simulation::SimManagementFrame::FRAME_TYPE_DISASSOC_REQ) {
    auto disassoc_req_frame = static_cast<const simulation::SimDisassocReqFrame*>(mgmt_frame);
    validateChannel(disassoc_req_frame->channel_);
    EXPECT_EQ(disassoc_req_frame->src_addr_, kApBssid);
    EXPECT_EQ(disassoc_req_frame->dst_addr_, kClientMacAddr);

    disassoc_req_count_++;
    disassoc_status_list_.push_back(disassoc_req_frame->reason_);
  } else {
    GTEST_FAIL();
  }
}

void AssocTest::ReceiveNotification(void* payload) {
  auto handler = static_cast<std::function<void()>*>(payload);
  (*handler)();
  delete handler;
}

/* Verify that association requests that are not properly addressed are ignored.

   Timeline for this test:
   1s: send assoc request on different channel
   2s: send assoc request with different bssid
 */
TEST_F(AssocTest, IgnoredRequests) {
  constexpr wlan_channel_t kWrongChannel = {
      .primary = 10, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
  static const common::MacAddr kWrongBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbd});

  // Schedule assoc req on different channel
  auto handler = new std::function<void()>;
  simulation::SimAssocReqFrame wrong_chan_frame(this, kWrongChannel, kClientMacAddr, kApBssid);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &wrong_chan_frame);
  env_.ScheduleNotification(this, zx::sec(1), static_cast<void*>(handler));

  // Schedule assoc req to different bssid
  handler = new std::function<void()>;
  simulation::SimAssocReqFrame wrong_bssid_frame(this, kDefaultChannel, kClientMacAddr,
                                                 kWrongBssid);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &wrong_bssid_frame);
  env_.ScheduleNotification(this, zx::sec(2), static_cast<void*>(handler));

  env_.Run();

  // Verify that no assoc responses were seen in the environment
  EXPECT_EQ(assoc_resp_count_, 0U);
}

/* Verify that several association requests sent in quick succession are all answered, and that
   only the first from a client is successful.

   Timeline for this test:
   50 usec: send assoc request
   100 usec: send assoc request
   150 usec: send assoc request
 */
TEST_F(AssocTest, BasicUse) {
  // Schedule first request
  auto handler = new std::function<void()>;
  simulation::SimAssocReqFrame assoc_req_frame(this, kDefaultChannel, kClientMacAddr, kApBssid);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &assoc_req_frame);
  env_.ScheduleNotification(this, zx::usec(50), static_cast<void*>(handler));

  // Schedule second request
  handler = new std::function<void()>;
  *handler = std::bind(&simulation::Environment::Tx, &env_, &assoc_req_frame);
  env_.ScheduleNotification(this, zx::usec(100), static_cast<void*>(handler));

  // Schedule third request
  handler = new std::function<void()>;
  *handler = std::bind(&simulation::Environment::Tx, &env_, &assoc_req_frame);
  env_.ScheduleNotification(this, zx::usec(150), static_cast<void*>(handler));

  env_.Run();

  EXPECT_EQ(assoc_resp_count_, 3U);
  ASSERT_EQ(assoc_status_list_.size(), (size_t)3);
  EXPECT_EQ(assoc_status_list_.front(), (uint16_t)WLAN_STATUS_CODE_SUCCESS);
  assoc_status_list_.pop_front();
  EXPECT_EQ(assoc_status_list_.front(), (uint16_t)WLAN_STATUS_CODE_REFUSED_TEMPORARILY);
  assoc_status_list_.pop_front();
  EXPECT_EQ(assoc_status_list_.front(), (uint16_t)WLAN_STATUS_CODE_REFUSED_TEMPORARILY);
  assoc_status_list_.pop_front();
}

/* Verify that association requests are ignored when the association handling state is set to
   ASSOC_IGNORED.

   Timeline for this test:
   1s: send assoc request
 */
TEST_F(AssocTest, IgnoreAssociations) {
  // Schedule assoc req
  auto handler = new std::function<void()>;
  simulation::SimAssocReqFrame assoc_req_frame(this, kDefaultChannel, kClientMacAddr, kApBssid);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &assoc_req_frame);
  env_.ScheduleNotification(this, zx::sec(1), static_cast<void*>(handler));

  ap_.SetAssocHandling(simulation::FakeAp::ASSOC_IGNORED);

  env_.Run();

  // Verify that no assoc responses were seen in the environment
  EXPECT_EQ(assoc_resp_count_, 0U);
}

/* Verify that association requests are rejected when the association handling state is set to
   ASSOC_REJECTED.

   Timeline for this test:
   1s: send assoc request
 */
TEST_F(AssocTest, RejectAssociations) {
  // Schedule first request
  auto handler = new std::function<void()>;
  simulation::SimAssocReqFrame assoc_req_frame(this, kDefaultChannel, kClientMacAddr, kApBssid);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &assoc_req_frame);
  env_.ScheduleNotification(this, zx::sec(1), static_cast<void*>(handler));

  ap_.SetAssocHandling(simulation::FakeAp::ASSOC_REJECTED);

  env_.Run();

  EXPECT_EQ(assoc_resp_count_, 1U);
  ASSERT_EQ(assoc_status_list_.size(), (size_t)1);
  EXPECT_EQ(assoc_status_list_.front(), (uint16_t)WLAN_STATUS_CODE_REFUSED);
  assoc_status_list_.pop_front();
}

/* Verify that Disassociation from previously associated STA is handled
   correctly.

   Timeline for this test:
   1s: send assoc request
   2s: send disassoc request
 */
TEST_F(AssocTest, DisassocFromSta) {
  // Schedule assoc req
  auto handler = new std::function<void()>;
  simulation::SimAssocReqFrame assoc_req_frame(this, kDefaultChannel, kClientMacAddr, kApBssid);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &assoc_req_frame);
  env_.ScheduleNotification(this, zx::sec(1), static_cast<void*>(handler));

  // Schedule Disassoc request from STA
  handler = new std::function<void()>;
  simulation::SimDisassocReqFrame disassoc_req_frame(this, kDefaultChannel, kClientMacAddr,
                                                     kApBssid, kClientDisassocReason);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &disassoc_req_frame);
  env_.ScheduleNotification(this, zx::sec(2), static_cast<void*>(handler));

  env_.Run();

  // Verify that one assoc resp was seen and after disassoc the number of
  // clients should be 0.
  EXPECT_EQ(assoc_resp_count_, 1U);
  ASSERT_EQ(assoc_status_list_.size(), (size_t)1);
  EXPECT_EQ(assoc_status_list_.front(), (uint16_t)WLAN_STATUS_CODE_SUCCESS);
  EXPECT_EQ(ap_.GetNumClients(), 0U);
  assoc_status_list_.pop_front();
}

/* Verify that Disassociation from the FakeAP is handled correctly.

   Timeline for this test:
   1s: send assoc request
   2s: send disassoc request from AP
 */
TEST_F(AssocTest, DisassocFromAp) {
  // Schedule assoc req
  auto handler = new std::function<void()>;
  simulation::SimAssocReqFrame assoc_req_frame(this, kDefaultChannel, kClientMacAddr, kApBssid);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &assoc_req_frame);
  env_.ScheduleNotification(this, zx::sec(1), static_cast<void*>(handler));

  // Schedule Disassoc request from AP
  handler = new std::function<void()>;
  *handler = std::bind(&AssocTest::DisassocFromAp, this, kClientMacAddr, kApDisassocReason);
  env_.ScheduleNotification(this, zx::sec(2), static_cast<void*>(handler));

  env_.Run();

  // Verify that one assoc resp was seen and after disassoc the number of
  // clients should be 0.
  EXPECT_EQ(assoc_resp_count_, 1U);
  ASSERT_EQ(assoc_status_list_.size(), (size_t)1);
  EXPECT_EQ(assoc_status_list_.front(), (uint16_t)WLAN_STATUS_CODE_SUCCESS);
  EXPECT_EQ(ap_.GetNumClients(), 0U);
  EXPECT_EQ(disassoc_req_count_, 1U);
  EXPECT_EQ(disassoc_status_list_.front(), kApDisassocReason);
  assoc_status_list_.pop_front();
  disassoc_status_list_.pop_front();
}
}  // namespace wlan::testing
