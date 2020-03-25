// Copyright 2020 The Fuchsia Authors. All rights reserved.
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

class BeaconLostTest : public SimTest {
 public:
  // How long an individual test will run for. We need an end time because tests run until no more
  // events remain and so we need to stop aps from beaconing to drain the event queue.
  static constexpr zx::duration kTestDuration = zx::sec(100);

  void Init();
  void Finish();

  // Run through the join => auth => assoc flow
  void StartAssoc();

  // Schedule a future call to a member function
  void ScheduleCall(void (BeaconLostTest::*fn)(), zx::duration when);

  // Move client's location in environment
  void MoveClient(int32_t x, int32_t y);

 protected:
  struct AssocContext {
    // Information about the BSS we are attempting to associate with. Used to generate the
    // appropriate MLME calls (Join => Auth => Assoc).
    simulation::WlanTxInfo tx_info = {.channel = kDefaultChannel};
    common::MacAddr bssid = kDefaultBssid;
    wlan_ssid_t ssid = kDefaultSsid;

    // There should be one result for each association response received
    std::list<wlan_assoc_result_t> expected_results;

    // An optional function to call when we see the association request go out
    std::optional<std::function<void()>> on_assoc_req_callback;

    // Track number of association responses
    size_t assoc_resp_count;
    // Track number of deauth indications, triggered by firmware
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

  // Once test is finished, associations and disassociations from teardowns are ignored
  bool test_complete_ = false;

 private:
  // StationIfc overrides
  void Rx(const simulation::SimFrame* frame, simulation::WlanRxInfo& info) override;
  void ReceiveNotification(void* payload) override;

  // SME callbacks
  static wlanif_impl_ifc_protocol_ops_t sme_ops_;
  wlanif_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};

  // Event handlers
  void OnJoinConf(const wlanif_join_confirm_t* resp);
  void OnAuthConf(const wlanif_auth_confirm_t* resp);
  void OnAssocConf(const wlanif_assoc_confirm_t* resp);
  void OnDeauthInd(const wlanif_deauth_indication_t* ind);
};

// Since we're acting as wlanif, we need handlers for any protocol calls we may receive
wlanif_impl_ifc_protocol_ops_t BeaconLostTest::sme_ops_ = {
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
          static_cast<BeaconLostTest*>(cookie)->OnJoinConf(resp);
        },
    .auth_conf =
        [](void* cookie, const wlanif_auth_confirm_t* resp) {
          static_cast<BeaconLostTest*>(cookie)->OnAuthConf(resp);
        },
    .deauth_ind =
        [](void* cookie, const wlanif_deauth_indication_t* ind) {
          static_cast<BeaconLostTest*>(cookie)->OnDeauthInd(ind);
        },
    .assoc_conf =
        [](void* cookie, const wlanif_assoc_confirm_t* resp) {
          static_cast<BeaconLostTest*>(cookie)->OnAssocConf(resp);
        },
    .disassoc_conf =
        [](void* cookie, const wlanif_disassoc_confirm_t* resp) {
          // ignore
        },
    .signal_report =
        [](void* cookie, const wlanif_signal_report_indication* ind) {
          // ignore
        },
};

void BeaconLostTest::ReceiveNotification(void* payload) {
  auto fn = static_cast<std::function<void()>*>(payload);
  (*fn)();
  delete fn;
}

void BeaconLostTest::Rx(const simulation::SimFrame* frame, simulation::WlanRxInfo& info) {
  ASSERT_EQ(frame->FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);

  auto mgmt_frame = static_cast<const simulation::SimManagementFrame*>(frame);
  // If a handler has been installed, call it
  if (mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_ASSOC_REQ) {
    if (context_.on_assoc_req_callback) {
      (*context_.on_assoc_req_callback)();
    }
  }
}

// Create our device instance and hook up the callbacks
void BeaconLostTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT, sme_protocol_, &client_ifc_), ZX_OK);
  context_.assoc_resp_count = 0;
  context_.deauth_ind_count = 0;
  ScheduleCall(&BeaconLostTest::Finish, kTestDuration);
}

void BeaconLostTest::Finish() {
  for (auto ap : aps_) {
    ap->DisableBeacon();
  }
  aps_.clear();
  test_complete_ = true;
}

void BeaconLostTest::OnJoinConf(const wlanif_join_confirm_t* resp) {
  // Send auth request
  wlanif_auth_req_t auth_req;
  std::memcpy(auth_req.peer_sta_address, context_.bssid.byte, ETH_ALEN);
  auth_req.auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM;
  auth_req.auth_failure_timeout = 1000;  // ~1s (although value is ignored for now)
  client_ifc_->if_impl_ops_->auth_req(client_ifc_->if_impl_ctx_, &auth_req);
}

void BeaconLostTest::OnAuthConf(const wlanif_auth_confirm_t* resp) {
  // Send assoc request
  wlanif_assoc_req_t assoc_req = {.rsne_len = 0, .vendor_ie_len = 0};
  memcpy(assoc_req.peer_sta_address, context_.bssid.byte, ETH_ALEN);
  client_ifc_->if_impl_ops_->assoc_req(client_ifc_->if_impl_ctx_, &assoc_req);
}

void BeaconLostTest::OnAssocConf(const wlanif_assoc_confirm_t* resp) {
  context_.assoc_resp_count++;
  EXPECT_EQ(resp->result_code, context_.expected_results.front());
  context_.expected_results.pop_front();
}

void BeaconLostTest::OnDeauthInd(const wlanif_deauth_indication_t* ind) {
  if (!test_complete_)
    context_.deauth_ind_count++;
}

void BeaconLostTest::StartAssoc() {
  // Send join request
  wlanif_join_req join_req = {};
  std::memcpy(join_req.selected_bss.bssid, context_.bssid.byte, ETH_ALEN);
  join_req.selected_bss.ssid.len = context_.ssid.len;
  memcpy(join_req.selected_bss.ssid.data, context_.ssid.ssid, WLAN_MAX_SSID_LEN);
  join_req.selected_bss.chan = context_.tx_info.channel;
  client_ifc_->if_impl_ops_->join_req(client_ifc_->if_impl_ctx_, &join_req);
}

void BeaconLostTest::ScheduleCall(void (BeaconLostTest::*fn)(), zx::duration when) {
  auto cb_fn = new std::function<void()>;
  *cb_fn = std::bind(fn, this);
  env_->ScheduleNotification(this, when, cb_fn);
}

// Move the client (not the test)
void BeaconLostTest::MoveClient(int32_t x, int32_t y) {
  env_->MoveStation(device_->GetSim()->sim_fw->GetHardwareIfc(), x, y);
}

// Verify that deauthorization occurs if associated AP's beacons disappear
TEST_F(BeaconLostTest, NoBeaconDisassocTest) {
  // Create our device instance
  Init();

  // Start up fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  // Do not add ap to list of ap's that need to have their beacon disabled, since we
  // will disable it in test
  context_.expected_results.push_front(WLAN_ASSOC_RESULT_SUCCESS);

  // Associate with fake AP
  ScheduleCall(&BeaconLostTest::StartAssoc, zx::msec(10));

  // disable the beacon after association
  auto cb_fn = new std::function<void()>;
  *cb_fn = std::bind(&simulation::FakeAp::DisableBeacon, &ap);
  env_->ScheduleNotification(this, zx::sec(1), cb_fn);

  env_->Run();

  // Association with fake AP should be successful
  EXPECT_EQ(context_.assoc_resp_count, 1U);
  // A disassociation should have occured due a beacon timeout
  EXPECT_EQ(context_.deauth_ind_count, 1U);
}

// Verify that deauthorization occurs after moving away from associated AP
// such that beacon is lost
// t = 0s
// +-------------------------------------------------+
// ap-client-----------------------------------------+
// +-------------------------------------------------+
// t = 1s
// +-------------------------------------------------+
// ap1-----------------------------------------client+
// +-------------------------------------------------+
TEST_F(BeaconLostTest, BeaconTooFarDisassocTest) {
  // Create our device instance
  Init();

  // Start up fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);
  context_.expected_results.push_front(WLAN_ASSOC_RESULT_SUCCESS);

  // Associate with fake AP
  ScheduleCall(&BeaconLostTest::StartAssoc, zx::msec(10));
  // Move away from the AP
  auto cb_fn = new std::function<void()>;
  *cb_fn = std::bind(&BeaconLostTest::MoveClient, this, 150, 0);
  env_->ScheduleNotification(this, zx::sec(1), cb_fn);

  env_->Run();

  // Association with fake AP should be successful
  EXPECT_EQ(context_.assoc_resp_count, 1U);
  // A disassociation should have occured due to moving away from the AP
  EXPECT_EQ(context_.deauth_ind_count, 1U);
}

// Verify that losing a beacon from an unassociated ap does not cause any disassociation
// t = 0s
// +-------------------------------------------------+
// -------ap1------------client-------------------ap2+
// +-------------------------------------------------+
// t = 1s
// +-------------------------------------------------+
// client-ap1-------------------------------------ap2+
// +-------------------------------------------------+
TEST_F(BeaconLostTest, WrongBeaconLossTest) {
  // Create our device instance
  Init();

  // Start up fake AP
  simulation::FakeAp ap1(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap1.EnableBeacon(zx::msec(100));
  constexpr wlan_ssid_t kWrongSsid = {.len = 14, .ssid = "Fuchsia Fake AP"};
  ASSERT_NE(kDefaultSsid.len, kWrongSsid.len);
  env_->MoveStation(&ap1, -50, 0);
  aps_.push_back(&ap1);
  simulation::FakeAp ap2(env_.get(), kDefaultBssid, kWrongSsid, kDefaultChannel);
  ap2.EnableBeacon(zx::msec(100));
  env_->MoveStation(&ap2, 50, 0);
  aps_.push_back(&ap2);

  context_.expected_results.push_front(WLAN_ASSOC_RESULT_SUCCESS);

  // Will associate with one AP
  ScheduleCall(&BeaconLostTest::StartAssoc, zx::msec(10));
  // Move away closer to the AP we are associated to. Should not impact connection.
  auto cb_fn = new std::function<void()>;
  *cb_fn = std::bind(&BeaconLostTest::MoveClient, this, -75, 0);
  env_->ScheduleNotification(this, zx::sec(1), cb_fn);

  env_->Run();

  // Association with fake AP should be successful
  EXPECT_EQ(context_.assoc_resp_count, 1U);
  // No disassociation should occur
  EXPECT_EQ(context_.deauth_ind_count, 0U);
}

// Verify that moving out of range of the beacon for a small duration does not cause a deauth
// t = 0s
// +-------------------------------------------------+
// ap1-client----------------------------------------+
// +-------------------------------------------------+
// t = 1s
// +-------------------------------------------------+
// ap1-------------------client----------------------+
// +-------------------------------------------------+
// t = 2s
// +-------------------------------------------------+
// ap1-client----------------------------------------+
// +-------------------------------------------------+
TEST_F(BeaconLostTest, TempBeaconLossTest) {
  // Create our device instance
  Init();

  // Start up fake AP
  simulation::FakeAp ap1(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap1.EnableBeacon(zx::msec(100));
  constexpr wlan_ssid_t kWrongSsid = {.len = 14, .ssid = "Fuchsia Fake AP"};
  ASSERT_NE(kDefaultSsid.len, kWrongSsid.len);
  env_->MoveStation(&ap1, 0, 0);
  aps_.push_back(&ap1);

  context_.expected_results.push_front(WLAN_ASSOC_RESULT_SUCCESS);

  // Will associate with one AP
  ScheduleCall(&BeaconLostTest::StartAssoc, zx::msec(10));
  // Move away from the AP we are associated to.
  auto cb_fn = new std::function<void()>;
  *cb_fn = std::bind(&BeaconLostTest::MoveClient, this, 100, 0);
  env_->ScheduleNotification(this, zx::sec(1), cb_fn);

  // A second later, move back
  auto cb_fn2 = new std::function<void()>;
  *cb_fn2 = std::bind(&BeaconLostTest::MoveClient, this, 0, 0);
  env_->ScheduleNotification(this, zx::sec(2), cb_fn2);

  env_->Run();

  // Association with fake AP should be successful
  EXPECT_EQ(context_.assoc_resp_count, 1U);
  // No disassociation should occur
  EXPECT_EQ(context_.deauth_ind_count, 0U);
}

}  // namespace wlan::brcmfmac
