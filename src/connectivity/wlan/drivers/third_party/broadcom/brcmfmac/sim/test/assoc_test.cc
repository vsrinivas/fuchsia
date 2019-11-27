// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/wlanif.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::brcmfmac {

// Some default AP and association request values
constexpr wlan_channel_t kDefaultChannel = {
    .primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_ssid_t kDefaultSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});

class AssocTest : public SimTest {
 public:
  // How long an individual test will run for. We need an end time because tests run until no more
  // events remain and so we need to stop aps from beaconing to drain the event queue.
  static constexpr zx::duration kTestDuration = zx::sec(100);

  void Init();
  void Finish();

  // Run through the join => auth => assoc flow
  void StartAssoc();

  // Schedule a future event that will attempt to associate (with settings and expected result
  // saved in context_
  void ScheduleAssocReq(zx::duration when);

  // Schedule an event to stop the test. This is needed to stop any beaconing APs, since the test
  // won't end until all events are processed.
  void ScheduleTestEnd(zx::duration when);

  // Send bad association responses
  void SendBadResp();

  // Send repeated association responses
  void SendMultipleResp();

 protected:
  struct AssocContext {
    // Information about the BSS we are attempting to associate with. Used to generate the
    // appropriate MLME calls (Join => Auth => Assoc).
    wlan_channel_t channel = kDefaultChannel;
    common::MacAddr bssid = kDefaultBssid;
    wlan_ssid_t ssid = kDefaultSsid;

    // There should be one result for each association response received
    std::list<wlan_assoc_result_t> expected_results;

    // An optional function to call when we see the association request go out
    std::optional<std::function<void()>> on_assoc_req_callback;

    // Track number of association responses
    size_t assoc_resp_count;
  };

  struct AssocRespInfo {
    wlan_channel_t channel;
    common::MacAddr src;
    common::MacAddr dst;
    uint16_t status;
  };

  // This is the interface we will use for our single client interface
  std::unique_ptr<SimInterface> client_ifc_;

  AssocContext context_;

  // Keep track of the APs that are in operation so we can easily disable beaconing on all of them
  // at the end of each test.
  std::list<simulation::FakeAp*> aps_;

  // All of the association responses seen in the environment
  std::list<AssocRespInfo> assoc_responses_;

 private:
  // StationIfc overrides
  void RxAssocReq(const wlan_channel_t& channel, const common::MacAddr& src,
                  const common::MacAddr& bssid) override;
  void RxAssocResp(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, uint16_t status) override;
  void ReceiveNotification(void* payload) override;

  // SME callbacks
  static wlanif_impl_ifc_protocol_ops_t sme_ops_;
  wlanif_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};

  // Event handlers
  void OnJoinConf(const wlanif_join_confirm_t* resp);
  void OnAuthConf(const wlanif_auth_confirm_t* resp);
  void OnAssocConf(const wlanif_assoc_confirm_t* resp);
};

// Since we're acting as wlanif, we need handlers for any protocol calls we may receive
wlanif_impl_ifc_protocol_ops_t AssocTest::sme_ops_ = {
    .on_scan_result =
        [](void* cookie, const wlanif_scan_result_t* result) {
          // Ignore
        },
    .on_scan_end =
        [](void* cookie, const wlanif_scan_end_t* end) {
          // Ignore
        },
    .join_conf =
        [](void* cookie, const wlanif_join_confirm_t* resp) {
          static_cast<AssocTest*>(cookie)->OnJoinConf(resp);
        },
    .auth_conf =
        [](void* cookie, const wlanif_auth_confirm_t* resp) {
          static_cast<AssocTest*>(cookie)->OnAuthConf(resp);
        },
    .assoc_conf =
        [](void* cookie, const wlanif_assoc_confirm_t* resp) {
          static_cast<AssocTest*>(cookie)->OnAssocConf(resp);
        },
};

void AssocTest::ReceiveNotification(void* payload) {
  auto fn = static_cast<std::function<void()>*>(payload);
  (*fn)();
  delete fn;
}

// If a handler has been installed, call it
void AssocTest::RxAssocReq(const wlan_channel_t& channel, const common::MacAddr& src,
                           const common::MacAddr& bssid) {
  if (context_.on_assoc_req_callback) {
    (*context_.on_assoc_req_callback)();
  }
}

void AssocTest::RxAssocResp(const wlan_channel_t& channel, const common::MacAddr& src,
                            const common::MacAddr& dst, uint16_t status) {
  AssocRespInfo resp_info = {.channel = channel, .src = src, .dst = dst, .status = status};
  assoc_responses_.push_back(resp_info);
}

// Create our device instance and hook up the callbacks
void AssocTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT, sme_protocol_, &client_ifc_), ZX_OK);
  context_.assoc_resp_count = 0;
  ScheduleTestEnd(kTestDuration);
}

void AssocTest::Finish() {
  for (auto ap : aps_) {
    ap->DisableBeacon();
  }
  aps_.clear();
}

void AssocTest::ScheduleTestEnd(zx::duration when) {
  auto end_test_fn = new std::function<void()>;
  *end_test_fn = std::bind(&AssocTest::Finish, this);
  env_->ScheduleNotification(this, when, end_test_fn);
}

void AssocTest::OnJoinConf(const wlanif_join_confirm_t* resp) {
  // Send auth request
  wlanif_auth_req_t auth_req;
  std::memcpy(auth_req.peer_sta_address, context_.bssid.byte, ETH_ALEN);
  auth_req.auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM;
  auth_req.auth_failure_timeout = 1000;  // ~1s (although value is ignored for now)
  client_ifc_->if_impl_ops_->auth_req(client_ifc_->if_impl_ctx_, &auth_req);
}

void AssocTest::OnAuthConf(const wlanif_auth_confirm_t* resp) {
  // Send assoc request
  wlanif_assoc_req_t assoc_req = {.rsne_len = 0, .vendor_ie_len = 0};
  memcpy(assoc_req.peer_sta_address, context_.bssid.byte, ETH_ALEN);
  client_ifc_->if_impl_ops_->assoc_req(client_ifc_->if_impl_ctx_, &assoc_req);
}

void AssocTest::OnAssocConf(const wlanif_assoc_confirm_t* resp) {
  context_.assoc_resp_count++;
  EXPECT_EQ(resp->result_code, context_.expected_results.front());
  context_.expected_results.pop_front();
}

void AssocTest::StartAssoc() {
  // Send join request
  wlanif_join_req join_req = {};
  std::memcpy(join_req.selected_bss.bssid, context_.bssid.byte, ETH_ALEN);
  join_req.selected_bss.ssid.len = context_.ssid.len;
  memcpy(join_req.selected_bss.ssid.data, context_.ssid.ssid, WLAN_MAX_SSID_LEN);
  join_req.selected_bss.chan = context_.channel;
  client_ifc_->if_impl_ops_->join_req(client_ifc_->if_impl_ctx_, &join_req);
}

void AssocTest::ScheduleAssocReq(zx::duration when) {
  auto start_assoc_fn = new std::function<void()>;
  *start_assoc_fn = std::bind(&AssocTest::StartAssoc, this);
  env_->ScheduleNotification(this, when, start_assoc_fn);
}

// For this test, we want the pre-assoc scan test to fail because no APs are found.
TEST_F(AssocTest, NoAps) {
  // Create our device instance
  Init();

  const common::MacAddr kBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
  context_.bssid = kBssid;
  context_.expected_results.push_front(WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED);
  context_.ssid = {.len = 6, .ssid = "TestAP"};
  context_.channel = {.primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};

  ScheduleAssocReq(zx::msec(10));

  env_->Run();

  EXPECT_EQ(context_.assoc_resp_count, 1U);
}

// Verify that we can successfully associate to a fake AP
TEST_F(AssocTest, SimpleTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  context_.expected_results.push_front(WLAN_ASSOC_RESULT_SUCCESS);

  ScheduleAssocReq(zx::msec(10));

  env_->Run();

  EXPECT_EQ(context_.assoc_resp_count, 1U);
}

// Verify that we can associate using only SSID, not BSSID
TEST_F(AssocTest, SsidTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  context_.bssid = wlan::common::kZeroMac;  // Use the wildcard BSSID
  context_.expected_results.push_front(WLAN_ASSOC_RESULT_SUCCESS);

  ScheduleAssocReq(zx::msec(10));

  env_->Run();

  EXPECT_EQ(context_.assoc_resp_count, 1U);
}

// Verify that APs with incorrect SSIDs or BSSIDs are ignored
TEST_F(AssocTest, WrongIds) {
  // Create our device instance
  Init();

  constexpr wlan_channel_t kWrongChannel = {
      .primary = 8, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
  ASSERT_NE(kDefaultChannel.primary, kWrongChannel.primary);
  constexpr wlan_ssid_t kWrongSsid = {.len = 14, .ssid = "Fuchsia Fake AP"};
  ASSERT_NE(kDefaultSsid.len, kWrongSsid.len);
  const common::MacAddr kWrongBssid({0x12, 0x34, 0x56, 0x78, 0x9b, 0xbc});
  ASSERT_NE(kDefaultBssid, kWrongBssid);

  // Start up fake APs
  simulation::FakeAp ap1(env_.get(), kDefaultBssid, kDefaultSsid, kWrongChannel);
  ap1.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap1);
  simulation::FakeAp ap2(env_.get(), kWrongBssid, kDefaultSsid, kDefaultChannel);
  ap2.EnableBeacon(zx::msec(90));
  aps_.push_back(&ap2);
  simulation::FakeAp ap3(env_.get(), kDefaultBssid, kWrongSsid, kDefaultChannel);
  ap3.EnableBeacon(zx::msec(80));
  aps_.push_back(&ap3);

  context_.expected_results.push_front(WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED);

  ScheduleAssocReq(zx::msec(10));

  env_->Run();

  // The APs aren't giving us a response, but the driver is telling us that the operation failed
  // because it couldn't find a matching AP.
  EXPECT_EQ(context_.assoc_resp_count, 1U);
}

// Attempt to associate while already associated
TEST_F(AssocTest, RepeatedAssocTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  // The associations at 11ms and 12ms should be immediately rejected (because there is already
  // an association in progress), and eventually the association that was in progress should
  // succeed
  context_.expected_results.push_back(WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED);
  context_.expected_results.push_back(WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED);
  context_.expected_results.push_back(WLAN_ASSOC_RESULT_SUCCESS);

  ScheduleAssocReq(zx::msec(10));
  ScheduleAssocReq(zx::msec(11));
  ScheduleAssocReq(zx::msec(12));

  env_->Run();

  EXPECT_EQ(context_.assoc_resp_count, 3U);
}

// Verify that if an AP does not respond to an association response we return a failure
TEST_F(AssocTest, ApIgnoredRequest) {
  // Create our device instance
  Init();

  // Start up fake APs
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  ap.SetAssocHandling(simulation::FakeAp::ASSOC_IGNORED);
  aps_.push_back(&ap);

  context_.expected_results.push_front(WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED);

  ScheduleAssocReq(zx::msec(10));

  env_->Run();

  // Make sure no responses were sent back from the fake AP
  EXPECT_EQ(assoc_responses_.size(), 0U);

  // But we still got our response from the driver
  EXPECT_EQ(context_.assoc_resp_count, 1U);
}

// Verify that if an AP rejects an association request we return a failure
TEST_F(AssocTest, ApRejectedRequest) {
  // Create our device instance
  Init();

  // Start up fake APs
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  ap.SetAssocHandling(simulation::FakeAp::ASSOC_REJECTED);
  aps_.push_back(&ap);

  context_.expected_results.push_front(WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED);

  ScheduleAssocReq(zx::msec(10));

  env_->Run();

  // We should have gotten a rejection from the fake AP
  EXPECT_EQ(assoc_responses_.size(), 1U);
  EXPECT_EQ(assoc_responses_.front().status, WLAN_STATUS_CODE_REFUSED);

  // Make sure we got our response from the driver
  EXPECT_EQ(context_.assoc_resp_count, 1U);
}

void AssocTest::SendBadResp() {
  // Figure out our own MAC
  uint8_t mac_buf[ETH_ALEN];
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->IovarsGet("cur_etheraddr", mac_buf, ETH_ALEN);
  common::MacAddr my_mac(mac_buf);

  // Send a response from the wrong bss
  common::MacAddr wrong_src(context_.bssid);
  wrong_src.byte[ETH_ALEN - 1]++;
  env_->TxAssocResp(this, context_.channel, wrong_src, my_mac, WLAN_ASSOC_RESULT_SUCCESS);

  // Send a response to a different STA
  common::MacAddr wrong_dst(my_mac);
  wrong_dst.byte[ETH_ALEN - 1]++;
  env_->TxAssocResp(this, context_.channel, context_.bssid, wrong_dst, WLAN_ASSOC_RESULT_SUCCESS);
}

// Verify that any non-applicable association responses (i.e., sent to or from the wrong MAC)
// are ignored
TEST_F(AssocTest, IgnoreRespMismatch) {
  // Create our device instance
  Init();

  // Start up fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));

  // We want the association request to be ignored so we can inject responses and verify that
  // they are being ignored.
  ap.SetAssocHandling(simulation::FakeAp::ASSOC_IGNORED);

  aps_.push_back(&ap);

  context_.expected_results.push_front(WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED);
  context_.on_assoc_req_callback = std::bind(&AssocTest::SendBadResp, this);

  ScheduleAssocReq(zx::msec(10));

  env_->Run();

  // Make sure that the firmware/driver ignored bad responses and sent back its own (failure)
  EXPECT_EQ(context_.assoc_resp_count, 1U);
}

void AssocTest::SendMultipleResp() {
  constexpr unsigned kRespCount = 100;

  // Figure out our own MAC
  uint8_t mac_buf[ETH_ALEN];
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->IovarsGet("cur_etheraddr", mac_buf, ETH_ALEN);
  common::MacAddr my_mac(mac_buf);

  for (unsigned i = 0; i < kRespCount; i++) {
    env_->TxAssocResp(this, context_.channel, context_.bssid, my_mac, WLAN_ASSOC_RESULT_SUCCESS);
  }
}

// Verify that responses after association are ignored
TEST_F(AssocTest, IgnoreExtraResp) {
  // Create our device instance
  Init();

  // Start up fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));

  // We want the association request to be ignored so we can inject responses and verify that
  // they are being ignored.
  ap.SetAssocHandling(simulation::FakeAp::ASSOC_IGNORED);

  aps_.push_back(&ap);

  context_.expected_results.push_front(WLAN_ASSOC_RESULT_SUCCESS);
  context_.on_assoc_req_callback = std::bind(&AssocTest::SendMultipleResp, this);

  ScheduleAssocReq(zx::msec(10));

  env_->Run();

  // Make sure that the firmware/driver only responded to the first response
  EXPECT_EQ(context_.assoc_resp_count, 1U);
}

// Attempt to associate while a scan is in-progress
TEST_F(AssocTest, AssocWhileScanning) {
  // Create our device instance
  Init();

  // Start up fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  context_.expected_results.push_front(WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED);
  context_.on_assoc_req_callback = std::bind(&AssocTest::SendMultipleResp, this);

  ScheduleAssocReq(zx::msec(10));

  wlanif_scan_req_t scan_req = {
      .txn_id = 42,
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
      .scan_type = WLAN_SCAN_TYPE_PASSIVE,
      .num_channels = 11,
      .channel_list = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      .min_channel_time = 100,
      .max_channel_time = 100,
      .num_ssids = 0,
  };
  client_ifc_->if_impl_ops_->start_scan(client_ifc_->if_impl_ctx_, &scan_req);

  env_->Run();

  EXPECT_EQ(context_.assoc_resp_count, 1U);
}

}  // namespace wlan::brcmfmac
