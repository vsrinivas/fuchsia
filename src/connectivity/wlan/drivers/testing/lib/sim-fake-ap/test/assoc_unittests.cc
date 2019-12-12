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

class AssocTest : public ::testing::Test, public simulation::StationIfc {
 public:
  AssocTest() : ap_(&env_, kApBssid, kApSsid, kDefaultChannel) { env_.AddStation(this); };

  simulation::Environment env_;
  simulation::FakeAp ap_;

  unsigned assoc_resp_count_ = 0;
  std::list<uint16_t> status_list_;

 private:
  // StationIfc methods
  void Rx(void* pkt) override { GTEST_FAIL(); }
  void RxBeacon(const wlan_channel_t& channel, const wlan_ssid_t& ssid,
                const common::MacAddr& bssid) override {
    GTEST_FAIL();
  };
  void RxAssocReq(const wlan_channel_t& channel, const common::MacAddr& src,
                  const common::MacAddr& bssid) override {
    GTEST_FAIL();
  }
  void RxAssocResp(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, uint16_t status) override;
  void RxDisassocReq(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, uint16_t reason) override {
    GTEST_FAIL();
  }
  void RxProbeReq(const wlan_channel_t& channel, const common::MacAddr& src) override {
    GTEST_FAIL();
  }
  void RxProbeResp(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, const wlan_ssid_t& ssid) override {
    GTEST_FAIL();
  }
  void ReceiveNotification(void* payload) override;
};

void validateChannel(const wlan_channel_t& channel) {
  EXPECT_EQ(channel.primary, kDefaultChannel.primary);
  EXPECT_EQ(channel.cbw, kDefaultChannel.cbw);
  EXPECT_EQ(channel.secondary80, kDefaultChannel.secondary80);
}

void AssocTest::RxAssocResp(const wlan_channel_t& channel, const common::MacAddr& src,
                            const common::MacAddr& dst, uint16_t status) {
  validateChannel(channel);
  EXPECT_EQ(src, kApBssid);
  EXPECT_EQ(dst, kClientMacAddr);

  assoc_resp_count_++;
  status_list_.push_back(status);
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
  *handler = std::bind(&simulation::Environment::TxAssocReq, &env_, this, kWrongChannel,
                       kClientMacAddr, kApBssid);
  env_.ScheduleNotification(this, zx::sec(1), static_cast<void*>(handler));

  // Schedule assoc req to different bssid
  handler = new std::function<void()>;
  *handler = std::bind(&simulation::Environment::TxAssocReq, &env_, this, kDefaultChannel,
                       kClientMacAddr, kWrongBssid);
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
  *handler = std::bind(&simulation::Environment::TxAssocReq, &env_, this, kDefaultChannel,
                       kClientMacAddr, kApBssid);
  env_.ScheduleNotification(this, zx::usec(50), static_cast<void*>(handler));

  // Schedule second request
  handler = new std::function<void()>;
  *handler = std::bind(&simulation::Environment::TxAssocReq, &env_, this, kDefaultChannel,
                       kClientMacAddr, kApBssid);
  env_.ScheduleNotification(this, zx::usec(100), static_cast<void*>(handler));

  // Schedule third request
  handler = new std::function<void()>;
  *handler = std::bind(&simulation::Environment::TxAssocReq, &env_, this, kDefaultChannel,
                       kClientMacAddr, kApBssid);
  env_.ScheduleNotification(this, zx::usec(150), static_cast<void*>(handler));

  env_.Run();

  EXPECT_EQ(assoc_resp_count_, 3U);
  ASSERT_EQ(status_list_.size(), (size_t)3);
  EXPECT_EQ(status_list_.front(), (uint16_t)WLAN_STATUS_CODE_SUCCESS);
  status_list_.pop_front();
  EXPECT_EQ(status_list_.front(), (uint16_t)WLAN_STATUS_CODE_REFUSED_TEMPORARILY);
  status_list_.pop_front();
  EXPECT_EQ(status_list_.front(), (uint16_t)WLAN_STATUS_CODE_REFUSED_TEMPORARILY);
  status_list_.pop_front();
}

/* Verify that association requests are ignored when the association handling state is set to
   ASSOC_IGNORED.

   Timeline for this test:
   1s: send assoc request
 */
TEST_F(AssocTest, IgnoreAssociations) {
  // Schedule assoc req
  auto handler = new std::function<void()>;
  *handler = std::bind(&simulation::Environment::TxAssocReq, &env_, this, kDefaultChannel,
                       kClientMacAddr, kApBssid);
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
  *handler = std::bind(&simulation::Environment::TxAssocReq, &env_, this, kDefaultChannel,
                       kClientMacAddr, kApBssid);
  env_.ScheduleNotification(this, zx::sec(1), static_cast<void*>(handler));

  ap_.SetAssocHandling(simulation::FakeAp::ASSOC_REJECTED);

  env_.Run();

  EXPECT_EQ(assoc_resp_count_, 1U);
  ASSERT_EQ(status_list_.size(), (size_t)1);
  EXPECT_EQ(status_list_.front(), (uint16_t)WLAN_STATUS_CODE_REFUSED);
  status_list_.pop_front();
}

}  // namespace wlan::testing
