// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::testing {
constexpr simulation::WlanTxInfo kDefaultTxInfo = {
    .channel = {.primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0}};
constexpr simulation::WlanTxInfo kWrongChannelTxInfo = {
    .channel = {.primary = 10, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0}};
constexpr wlan_ssid_t kApSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kApBssid({0x11, 0x11, 0x11, 0x11, 0x11, 0x11});
static const common::MacAddr kWrongBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbd});
const common::MacAddr kClientMacAddr({0x22, 0x22, 0x22, 0x22, 0x22, 0x22});

void validateChannel(const wlan_channel_t& channel) {
  EXPECT_EQ(channel.primary, kDefaultTxInfo.channel.primary);
  EXPECT_EQ(channel.cbw, kDefaultTxInfo.channel.cbw);
  EXPECT_EQ(channel.secondary80, kDefaultTxInfo.channel.secondary80);
}

class AuthTest : public ::testing::Test, public simulation::StationIfc {
 public:
  struct AuthResp {
    AuthResp(uint16_t seq_num, simulation::SimAuthType auth_type, uint16_t status)
        : seq_num_(seq_num), auth_type_(auth_type), status_(status){};

    uint16_t seq_num_;
    simulation::SimAuthType auth_type_;
    uint16_t status_;
  };

  AuthTest() : ap_(&env_, kApBssid, kApSsid, kDefaultTxInfo.channel) { env_.AddStation(this); }
  ~AuthTest() { env_.RemoveStation(this); }

  void ValidateAuthResp(uint16_t seq_num, simulation::SimAuthType auth_type, uint16_t status);
  simulation::Environment env_;
  simulation::FakeAp ap_;
  std::list<AuthResp> auth_resps_received_;

 private:
  void Rx(const simulation::SimFrame* frame, simulation::WlanRxInfo& info) override;
  void ReceiveNotification(void* payload) override;
};

void AuthTest::ValidateAuthResp(uint16_t expect_seq_num, simulation::SimAuthType expect_auth_type,
                                uint16_t expect_status) {
  EXPECT_EQ(auth_resps_received_.front().seq_num_, expect_seq_num);
  EXPECT_EQ(auth_resps_received_.front().auth_type_, expect_auth_type);
  EXPECT_EQ(auth_resps_received_.front().status_, expect_status);
  auth_resps_received_.pop_front();
}

void AuthTest::Rx(const simulation::SimFrame* frame, simulation::WlanRxInfo& info) {
  ASSERT_EQ(frame->FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);
  validateChannel(info.channel);

  auto mgmt_frame = static_cast<const simulation::SimManagementFrame*>(frame);
  // Ignore assoc resp
  if (mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_ASSOC_RESP) {
    return;
  }
  ASSERT_EQ(mgmt_frame->MgmtFrameType(), simulation::SimManagementFrame::FRAME_TYPE_AUTH);

  auto auth_resp_frame = static_cast<const simulation::SimAuthFrame*>(mgmt_frame);

  EXPECT_EQ(auth_resp_frame->src_addr_, kApBssid);
  EXPECT_EQ(auth_resp_frame->dst_addr_, kClientMacAddr);

  auth_resps_received_.emplace_back(auth_resp_frame->seq_num_, auth_resp_frame->auth_type_,
                                    auth_resp_frame->status_);
}

void AuthTest::ReceiveNotification(void* payload) {
  auto handler = static_cast<std::function<void()>*>(payload);
  (*handler)();
  delete handler;
}

TEST_F(AuthTest, OpenSystemBasicUse) {
  ap_.SetAuthType(simulation::AUTH_TYPE_OPEN);

  auto handler = new std::function<void()>;
  simulation::SimAuthFrame auth_req_frame(kClientMacAddr, kApBssid, 1, simulation::AUTH_TYPE_OPEN,
                                          WLAN_STATUS_CODE_SUCCESS);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &auth_req_frame, kDefaultTxInfo, this);
  env_.ScheduleNotification(this, zx::sec(1), static_cast<void*>(handler));

  env_.Run();

  ValidateAuthResp(2, simulation::AUTH_TYPE_OPEN, WLAN_STATUS_CODE_SUCCESS);

  EXPECT_EQ(auth_resps_received_.empty(), true);
}

TEST_F(AuthTest, SharedKeyBasicUse) {
  ap_.SetAuthType(simulation::AUTH_TYPE_SHARED_KEY);

  auto handler = new std::function<void()>;
  simulation::SimAuthFrame auth_req_frame1(
      kClientMacAddr, kApBssid, 1, simulation::AUTH_TYPE_SHARED_KEY, WLAN_STATUS_CODE_SUCCESS);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &auth_req_frame1, kDefaultTxInfo, this);
  env_.ScheduleNotification(this, zx::sec(1), static_cast<void*>(handler));

  handler = new std::function<void()>;
  simulation::SimAuthFrame auth_req_frame2(
      kClientMacAddr, kApBssid, 3, simulation::AUTH_TYPE_SHARED_KEY, WLAN_STATUS_CODE_SUCCESS);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &auth_req_frame2, kDefaultTxInfo, this);
  env_.ScheduleNotification(this, zx::sec(2), static_cast<void*>(handler));

  env_.Run();

  ValidateAuthResp(2, simulation::AUTH_TYPE_SHARED_KEY, WLAN_STATUS_CODE_SUCCESS);
  ValidateAuthResp(4, simulation::AUTH_TYPE_SHARED_KEY, WLAN_STATUS_CODE_SUCCESS);

  EXPECT_EQ(auth_resps_received_.empty(), true);
}

TEST_F(AuthTest, OpenSystemIgnoreTest) {
  ap_.SetAuthType(simulation::AUTH_TYPE_OPEN);

  auto handler = new std::function<void()>;
  simulation::SimAuthFrame auth_req_frame1(kClientMacAddr, kApBssid, 3, simulation::AUTH_TYPE_OPEN,
                                           WLAN_STATUS_CODE_SUCCESS);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &auth_req_frame1, kDefaultTxInfo, this);
  env_.ScheduleNotification(this, zx::sec(1), static_cast<void*>(handler));

  // Associated station will be ignored
  handler = new std::function<void()>;
  simulation::SimAuthFrame auth_req_frame(kClientMacAddr, kApBssid, 1, simulation::AUTH_TYPE_OPEN,
                                          WLAN_STATUS_CODE_SUCCESS);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &auth_req_frame, kDefaultTxInfo, this);
  env_.ScheduleNotification(this, zx::sec(2), static_cast<void*>(handler));

  handler = new std::function<void()>;
  simulation::SimAssocReqFrame assoc_req_frame(kClientMacAddr, kApBssid);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &assoc_req_frame, kDefaultTxInfo, this);
  env_.ScheduleNotification(this, zx::sec(3), static_cast<void*>(handler));

  handler = new std::function<void()>;
  simulation::SimAuthFrame auth_after_assoc(kClientMacAddr, kApBssid, 1, simulation::AUTH_TYPE_OPEN,
                                            WLAN_STATUS_CODE_SUCCESS);
  *handler =
      std::bind(&simulation::Environment::Tx, &env_, &auth_after_assoc, kDefaultTxInfo, this);
  env_.ScheduleNotification(this, zx::sec(4), static_cast<void*>(handler));

  env_.Run();
  // The second resp will be ignored
  EXPECT_EQ(auth_resps_received_.size(), (size_t)1);
}

TEST_F(AuthTest, SharedKeyIgnoreTest) {
  ap_.SetAuthType(simulation::AUTH_TYPE_SHARED_KEY);
  // Wrong bssid frame should be ignore
  auto handler = new std::function<void()>;
  simulation::SimAuthFrame wrong_bssid_frame(
      kClientMacAddr, kWrongBssid, 1, simulation::AUTH_TYPE_SHARED_KEY, WLAN_STATUS_CODE_SUCCESS);
  *handler =
      std::bind(&simulation::Environment::Tx, &env_, &wrong_bssid_frame, kDefaultTxInfo, this);
  env_.ScheduleNotification(this, zx::sec(1), static_cast<void*>(handler));

  // Wrong channel frame should be ignore
  handler = new std::function<void()>;
  simulation::SimAuthFrame wrong_channel_frame(
      kClientMacAddr, kApBssid, 1, simulation::AUTH_TYPE_SHARED_KEY, WLAN_STATUS_CODE_SUCCESS);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &wrong_channel_frame,
                       kWrongChannelTxInfo, this);
  env_.ScheduleNotification(this, zx::sec(2), static_cast<void*>(handler));

  // auth req with status WLAN_STATUS_CODE_REFUSED should be ignored
  handler = new std::function<void()>;
  simulation::SimAuthFrame refuse_frame(kClientMacAddr, kApBssid, 1,
                                        simulation::AUTH_TYPE_SHARED_KEY, WLAN_STATUS_CODE_REFUSED);
  *handler = std::bind(&simulation::Environment::Tx, &env_, &refuse_frame, kDefaultTxInfo, this);
  env_.ScheduleNotification(this, zx::sec(3), static_cast<void*>(handler));

  // auth req with sequence number 2 should be ignored
  handler = new std::function<void()>;
  simulation::SimAuthFrame seq_num_two_frame(
      kClientMacAddr, kApBssid, 2, simulation::AUTH_TYPE_SHARED_KEY, WLAN_STATUS_CODE_SUCCESS);
  *handler =
      std::bind(&simulation::Environment::Tx, &env_, &seq_num_two_frame, kDefaultTxInfo, this);
  env_.ScheduleNotification(this, zx::sec(4), static_cast<void*>(handler));

  // auth req with sequence number 4 should be ignored
  handler = new std::function<void()>;
  simulation::SimAuthFrame seq_num_four_frame(
      kClientMacAddr, kApBssid, 4, simulation::AUTH_TYPE_SHARED_KEY, WLAN_STATUS_CODE_SUCCESS);
  *handler =
      std::bind(&simulation::Environment::Tx, &env_, &seq_num_four_frame, kDefaultTxInfo, this);
  env_.ScheduleNotification(this, zx::sec(5), static_cast<void*>(handler));

  env_.Run();
  EXPECT_EQ(auth_resps_received_.empty(), true);
}

TEST_F(AuthTest, OpenSystemRefuseTest) {
  ap_.SetAuthType(simulation::AUTH_TYPE_OPEN);

  auto handler = new std::function<void()>;
  simulation::SimAuthFrame wrong_type_frame(
      kClientMacAddr, kApBssid, 1, simulation::AUTH_TYPE_SHARED_KEY, WLAN_STATUS_CODE_SUCCESS);
  *handler =
      std::bind(&simulation::Environment::Tx, &env_, &wrong_type_frame, kDefaultTxInfo, this);
  env_.ScheduleNotification(this, zx::sec(3), static_cast<void*>(handler));
  env_.Run();
  // The auth type in frame is the same as that in auth req frame, and if auth type in quth req is
  // different from that in AP's auth_handling_mode_, it will reply a refuse auth resp frame.
  ValidateAuthResp(2, simulation::AUTH_TYPE_SHARED_KEY, WLAN_STATUS_CODE_REFUSED);

  EXPECT_EQ(auth_resps_received_.empty(), true);
}

TEST_F(AuthTest, SharedKeyRefuseTest) {
  ap_.SetAuthType(simulation::AUTH_TYPE_SHARED_KEY);

  auto handler = new std::function<void()>;
  simulation::SimAuthFrame wrong_type_frame(kClientMacAddr, kApBssid, 1, simulation::AUTH_TYPE_OPEN,
                                            WLAN_STATUS_CODE_SUCCESS);
  *handler =
      std::bind(&simulation::Environment::Tx, &env_, &wrong_type_frame, kDefaultTxInfo, this);
  env_.ScheduleNotification(this, zx::sec(3), static_cast<void*>(handler));
  env_.Run();

  ValidateAuthResp(2, simulation::AUTH_TYPE_OPEN, WLAN_STATUS_CODE_REFUSED);

  EXPECT_EQ(auth_resps_received_.empty(), true);
}

}  // namespace wlan::testing
