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
const common::MacAddr kMadeupClient({0xde, 0xad, 0xbe, 0xef, 0x00, 0x01});
const uint16_t kDefaultApDisassocReason = 1;

class AssocTest : public SimTest {
 public:
  // How long an individual test will run for. We need an end time because tests run until no more
  // events remain and so we need to stop aps from beaconing to drain the event queue.
  static constexpr zx::duration kTestDuration = zx::sec(100);

  void Init();
  void Finish();

  // Run through the join => auth => assoc flow
  void StartAssoc();
  void StartDisassoc();
  void DisassocFromAp();

  // Schedule a future call to a member function
  void ScheduleCall(void (AssocTest::*fn)(), zx::duration when);

  // Send bad association responses
  void SendBadResp();

  // Send repeated association responses
  void SendMultipleResp();

  // Send Disassociate request to SIM FW
  void DisassocClient(const common::MacAddr& mac_addr);
  // Pretend to transmit Disassoc from AP
  void TxFakeDisassocReq();

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
    // Track number of disassociation confs (initiated from self)
    size_t disassoc_conf_count;
    // Track number of deauth indications (initiated from AP)
    size_t deauth_ind_count;
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

  // Trigger to start disassociation. If set to true, disassociation is started
  // soon after association completes.
  bool start_disassoc_ = false;
  // If disassoc_from_ap_ is set to true, the disassociation process is started
  // from the FakeAP else from the station itself.
  bool disassoc_from_ap_ = false;

  bool disassoc_self_ = true;

 private:
  // StationIfc overrides
  void Rx(const simulation::SimFrame* frame, const wlan_channel_t& channel) override;
  void ReceiveNotification(void* payload) override;

  // SME callbacks
  static wlanif_impl_ifc_protocol_ops_t sme_ops_;
  wlanif_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};

  // Event handlers
  void OnJoinConf(const wlanif_join_confirm_t* resp);
  void OnAuthConf(const wlanif_auth_confirm_t* resp);
  void OnAssocConf(const wlanif_assoc_confirm_t* resp);
  void OnDisassocConf(const wlanif_disassoc_confirm_t* resp);
  void OnDeauthInd(const wlanif_deauth_indication_t* ind);
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
    .deauth_ind =
        [](void* cookie, const wlanif_deauth_indication_t* ind) {
          static_cast<AssocTest*>(cookie)->OnDeauthInd(ind);
        },
    .assoc_conf =
        [](void* cookie, const wlanif_assoc_confirm_t* resp) {
          static_cast<AssocTest*>(cookie)->OnAssocConf(resp);
        },
    .disassoc_conf =
        [](void* cookie, const wlanif_disassoc_confirm_t* resp) {
          static_cast<AssocTest*>(cookie)->OnDisassocConf(resp);
        },
};

void AssocTest::ReceiveNotification(void* payload) {
  auto fn = static_cast<std::function<void()>*>(payload);
  (*fn)();
  delete fn;
}

void AssocTest::Rx(const simulation::SimFrame* frame, const wlan_channel_t& channel) {
  ASSERT_EQ(frame->FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);

  auto mgmt_frame = static_cast<const simulation::SimManagementFrame*>(frame);
  // If a handler has been installed, call it
  if (mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_ASSOC_REQ) {
    if (context_.on_assoc_req_callback) {
      (*context_.on_assoc_req_callback)();
    }
  }

  if (mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_ASSOC_RESP) {
    auto assoc_resp = static_cast<const simulation::SimAssocRespFrame*>(mgmt_frame);
    AssocRespInfo resp_info = {.channel = channel,
                               .src = assoc_resp->src_addr_,
                               .dst = assoc_resp->dst_addr_,
                               .status = assoc_resp->status_};
    assoc_responses_.push_back(resp_info);
  }
}

// Create our device instance and hook up the callbacks
void AssocTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT, sme_protocol_, &client_ifc_), ZX_OK);
  context_.assoc_resp_count = 0;
  context_.disassoc_conf_count = 0;
  context_.deauth_ind_count = 0;
  ScheduleCall(&AssocTest::Finish, kTestDuration);
}

void AssocTest::Finish() {
  for (auto ap : aps_) {
    ap->DisableBeacon();
  }
  aps_.clear();
}

void AssocTest::DisassocFromAp() {
  uint8_t mac_buf[ETH_ALEN];
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->IovarsGet("cur_etheraddr", mac_buf, ETH_ALEN);
  common::MacAddr my_mac(mac_buf);

  // Disassoc the STA
  for (auto ap : aps_) {
    ap->DisassocSta(my_mac, kDefaultApDisassocReason);
  }
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
  if (start_disassoc_) {
    std::function<void()>* callback = new std::function<void()>;
    *callback = std::bind(&AssocTest::StartDisassoc, this);
    env_->ScheduleNotification(this, zx::msec(100), static_cast<void*>(callback));
  }
}

void AssocTest::OnDisassocConf(const wlanif_disassoc_confirm_t* resp) {
  if (resp->status == ZX_OK) {
    context_.disassoc_conf_count++;
  }
}

void AssocTest::OnDeauthInd(const wlanif_deauth_indication_t* ind) { context_.deauth_ind_count++; }

void AssocTest::StartAssoc() {
  // Send join request
  wlanif_join_req join_req = {};
  std::memcpy(join_req.selected_bss.bssid, context_.bssid.byte, ETH_ALEN);
  join_req.selected_bss.ssid.len = context_.ssid.len;
  memcpy(join_req.selected_bss.ssid.data, context_.ssid.ssid, WLAN_MAX_SSID_LEN);
  join_req.selected_bss.chan = context_.channel;
  client_ifc_->if_impl_ops_->join_req(client_ifc_->if_impl_ctx_, &join_req);
}

void AssocTest::ScheduleCall(void (AssocTest::*fn)(), zx::duration when) {
  auto cb_fn = new std::function<void()>;
  *cb_fn = std::bind(fn, this);
  env_->ScheduleNotification(this, when, cb_fn);
}

void AssocTest::StartDisassoc() {
  // Send disassoc request
  if (!disassoc_from_ap_) {
    if (disassoc_self_) {
      DisassocClient(context_.bssid);
    } else {
      DisassocClient(kMadeupClient);
    }
  } else {
    DisassocFromAp();
  }
}

void AssocTest::DisassocClient(const common::MacAddr& mac_addr) {
  wlanif_disassoc_req disassoc_req = {};

  std::memcpy(disassoc_req.peer_sta_address, mac_addr.byte, ETH_ALEN);
  client_ifc_->if_impl_ops_->disassoc_req(client_ifc_->if_impl_ctx_, &disassoc_req);
}

void AssocTest::TxFakeDisassocReq() {
  // Figure out our own MAC
  uint8_t mac_buf[ETH_ALEN];
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->IovarsGet("cur_etheraddr", mac_buf, ETH_ALEN);
  common::MacAddr my_mac(mac_buf);

  // Send a Disassoc Req to our STA (which is not associated)
  simulation::SimDisassocReqFrame not_associated_frame(this, context_.bssid, my_mac,
                                                       kDefaultApDisassocReason);
  env_->Tx(&not_associated_frame, context_.channel);

  // Send a Disassoc Req from the wrong bss
  common::MacAddr wrong_src(context_.bssid);
  wrong_src.byte[ETH_ALEN - 1]++;
  simulation::SimDisassocReqFrame wrong_bss_frame(this, wrong_src, my_mac,
                                                  kDefaultApDisassocReason);
  env_->Tx(&wrong_bss_frame, context_.channel);

  // Send a Disassoc Req to a different STA
  common::MacAddr wrong_dst(my_mac);
  wrong_dst.byte[ETH_ALEN - 1]++;
  simulation::SimDisassocReqFrame wrong_sta_frame(this, context_.bssid, wrong_dst,
                                                  kDefaultApDisassocReason);
  env_->Tx(&wrong_sta_frame, context_.channel);
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

  ScheduleCall(&AssocTest::StartAssoc, zx::msec(10));

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

  ScheduleCall(&AssocTest::StartAssoc, zx::msec(10));

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

  ScheduleCall(&AssocTest::StartAssoc, zx::msec(10));

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

  ScheduleCall(&AssocTest::StartAssoc, zx::msec(10));

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

  ScheduleCall(&AssocTest::StartAssoc, zx::msec(10));
  ScheduleCall(&AssocTest::StartAssoc, zx::msec(11));
  ScheduleCall(&AssocTest::StartAssoc, zx::msec(12));

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

  ScheduleCall(&AssocTest::StartAssoc, zx::msec(10));

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

  ScheduleCall(&AssocTest::StartAssoc, zx::msec(10));

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
  simulation::SimAssocRespFrame wrong_bss_frame(this, wrong_src, my_mac, WLAN_ASSOC_RESULT_SUCCESS);
  env_->Tx(&wrong_bss_frame, context_.channel);

  // Send a response to a different STA
  common::MacAddr wrong_dst(my_mac);
  wrong_dst.byte[ETH_ALEN - 1]++;
  simulation::SimAssocRespFrame wrong_dst_frame(this, context_.bssid, wrong_dst,
                                                WLAN_ASSOC_RESULT_SUCCESS);
  env_->Tx(&wrong_dst_frame, context_.channel);
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

  ScheduleCall(&AssocTest::StartAssoc, zx::msec(10));

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
  simulation::SimAssocRespFrame multiple_resp_frame(this, context_.bssid, my_mac,
                                                    WLAN_ASSOC_RESULT_SUCCESS);
  for (unsigned i = 0; i < kRespCount; i++) {
    env_->Tx(&multiple_resp_frame, context_.channel);
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

  ScheduleCall(&AssocTest::StartAssoc, zx::msec(10));

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

  ScheduleCall(&AssocTest::StartAssoc, zx::msec(10));

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

// Verify that we can successfully associate to a fake AP & disassociate
TEST_F(AssocTest, DisassocFromSelfTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  context_.expected_results.push_front(WLAN_ASSOC_RESULT_SUCCESS);

  ScheduleCall(&AssocTest::StartAssoc, zx::msec(10));
  start_disassoc_ = true;

  env_->Run();

  EXPECT_EQ(context_.assoc_resp_count, 1U);
  EXPECT_EQ(context_.disassoc_conf_count, 1U);
}

// Verify that disassoc from fake AP fails when not associated. Also check
// disassoc meant for a different STA, different BSS or when not associated
// is not accepted by the current STA.
TEST_F(AssocTest, DisassocWithoutAssocTest) {
  // Create our device instance
  Init();
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  // Attempt to disassociate. In this case client is not associated. AP
  // will not transmit the disassoc request
  ScheduleCall(&AssocTest::StartDisassoc, zx::msec(10));
  ScheduleCall(&AssocTest::TxFakeDisassocReq, zx::msec(50));

  env_->Run();

  EXPECT_EQ(context_.assoc_resp_count, 0U);
  EXPECT_EQ(context_.disassoc_conf_count, 0U);
}

// Verify that disassociate for a different client is ignored
TEST_F(AssocTest, DisassocNotSelfTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  context_.expected_results.push_front(WLAN_ASSOC_RESULT_SUCCESS);

  ScheduleCall(&AssocTest::StartAssoc, zx::msec(10));
  start_disassoc_ = true;
  disassoc_self_ = false;

  env_->Run();

  EXPECT_EQ(context_.assoc_resp_count, 1U);
  EXPECT_EQ(context_.disassoc_conf_count, 0U);
}

// After association, send disassoc from the AP
TEST_F(AssocTest, DisassocFromAPTest) {
  // Create our device instance
  Init();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  context_.expected_results.push_front(WLAN_ASSOC_RESULT_SUCCESS);

  ScheduleCall(&AssocTest::StartAssoc, zx::msec(10));
  disassoc_from_ap_ = true;
  start_disassoc_ = true;

  env_->Run();

  EXPECT_EQ(context_.assoc_resp_count, 1U);
  EXPECT_EQ(context_.deauth_ind_count, 1U);
}
}  // namespace wlan::brcmfmac
