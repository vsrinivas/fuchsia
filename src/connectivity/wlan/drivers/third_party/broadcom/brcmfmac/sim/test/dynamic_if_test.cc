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
constexpr uint16_t kDefaultCh = 149;
constexpr wlan_channel_t kDefaultChannel = {
    .primary = kDefaultCh, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
// Chanspec value corresponding to kDefaultChannel with current d11 encoder.
constexpr uint16_t kDefaultChanpsec = 53397;
constexpr simulation::WlanTxInfo kDefaultTxInfo = {.channel = kDefaultChannel};
constexpr wlan_ssid_t kDefaultSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
const common::MacAddr kFakeMac({0xde, 0xad, 0xbe, 0xef, 0x00, 0x02});

class DynamicIfTest : public SimTest {
 public:
  // How long an individual test will run for. We need an end time because tests run until no more
  // events remain and so we need to stop aps from beaconing to drain the event queue.
  static constexpr zx::duration kTestDuration = zx::sec(100);
  DynamicIfTest() = default;
  void Init();
  void Finish();

  void CreateInterface(wlan_info_mac_role_t role);
  void DeleteInterface(wlan_info_mac_role_t role);
  uint32_t DeviceCount();
  zx_status_t StartSoftAP();

  // Run through the join => auth => assoc flow
  void StartAssoc();

  // Schedule a future event that will attempt to associate (with settings and expected result
  // saved in context_) with a FakeAp
  void ScheduleAssocReq(zx::duration when);

  // Schedule a future event that will attempt to associate Fake Client with our SoftAP
  void ScheduleAssocWithSoftAP(zx::duration when);

  // Schedule an event to stop the test. This is needed to stop any beaconing APs, since the test
  // won't end until all events are processed.
  void ScheduleTestEnd(zx::duration when);
  void TxAssocReq();

  void VerifyAssoc();

  // Query for wlanphy info
  void Query(wlanphy_impl_info_t* out_info);

  // Interfaces to set and get chanspec iovar in sim-fw
  void SetChanspec(bool is_ap_iface, uint16_t* chanspec, zx_status_t expect_result);
  uint16_t GetChanspec(bool is_ap_iface, zx_status_t expect_result);

 protected:
  struct AssocContext {
    // Information about the BSS we are attempting to associate with. Used to generate the
    // appropriate MLME calls (Join => Auth => Assoc).
    wlan_channel_t channel = kDefaultChannel;
    common::MacAddr bssid = kDefaultBssid;
    wlan_ssid_t ssid = kDefaultSsid;

    // There should be one result for each association response received
    std::list<wlan_assoc_result_t> expected_results;

    // Track number of association responses
    size_t assoc_resp_count;
  };

  struct AssocRespInfo {
    wlan_channel_t channel;
    common::MacAddr src;
    common::MacAddr dst;
    uint16_t status;
  };

  AssocContext context_;
  // Keep track of the APs that are in operation so we can easily disable beaconing on all of them
  // at the end of each test.
  std::list<simulation::FakeAp*> aps_;

  // All of the association responses seen in the environment
  std::list<AssocRespInfo> assoc_responses_;

 private:
  bool auth_ind_recv_ = false;
  bool assoc_ind_recv_ = false;
  // StationIfc overrides
  void Rx(const simulation::SimFrame* frame, simulation::WlanRxInfo& info) override;
  void ReceiveNotification(void* payload) override;

  // SME callbacks
  static wlanif_impl_ifc_protocol_ops_t sme_ops_;
  wlanif_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};
  std::unique_ptr<SimInterface> client_ifc_;
  std::unique_ptr<SimInterface> softap_ifc_;

  // Event handlers
  void OnJoinConf(const wlanif_join_confirm_t* resp);
  void OnAuthConf(const wlanif_auth_confirm_t* resp);
  void OnAssocConf(const wlanif_assoc_confirm_t* resp);
  void OnAuthInd(const wlanif_auth_ind_t* ind);
  void OnDeauthInd(const wlanif_deauth_indication_t* ind){};
  void OnAssocInd(const wlanif_assoc_ind_t* ind);
  void OnDisassocInd(const wlanif_disassoc_indication_t* ind){};
};

// Since we're acting as wlanif, we need handlers for any protocol calls we may receive
wlanif_impl_ifc_protocol_ops_t DynamicIfTest::sme_ops_ = {
    .join_conf =
        [](void* cookie, const wlanif_join_confirm_t* resp) {
          static_cast<DynamicIfTest*>(cookie)->OnJoinConf(resp);
        },
    .auth_conf =
        [](void* cookie, const wlanif_auth_confirm_t* resp) {
          static_cast<DynamicIfTest*>(cookie)->OnAuthConf(resp);
        },
    .auth_ind =
        [](void* cookie, const wlanif_auth_ind_t* ind) {
          static_cast<DynamicIfTest*>(cookie)->OnAuthInd(ind);
        },
    .deauth_ind =
        [](void* cookie, const wlanif_deauth_indication_t* ind) {
          // Ignore
        },
    .assoc_conf =
        [](void* cookie, const wlanif_assoc_confirm_t* resp) {
          static_cast<DynamicIfTest*>(cookie)->OnAssocConf(resp);
        },
    .assoc_ind =
        [](void* cookie, const wlanif_assoc_ind_t* ind) {
          static_cast<DynamicIfTest*>(cookie)->OnAssocInd(ind);
        },
    .start_conf =
        [](void* cookie, const wlanif_start_confirm_t* resp) {
          ASSERT_EQ(resp->result_code, WLAN_START_RESULT_SUCCESS);
        },
    .stop_conf =
        [](void* cookie, const wlanif_stop_confirm_t* resp) {
          ASSERT_EQ(resp->result_code, WLAN_STOP_RESULT_SUCCESS);
        },
    .signal_report = [](void* cookie, const wlanif_signal_report_indication* ind) {},
};

void DynamicIfTest::CreateInterface(wlan_info_mac_role_t role) {
  zx_status_t status;

  if (role == WLAN_INFO_MAC_ROLE_CLIENT)
    status = SimTest::CreateInterface(role, sme_protocol_, &client_ifc_);
  else
    status = SimTest::CreateInterface(role, sme_protocol_, &softap_ifc_);
  ASSERT_EQ(status, ZX_OK);
}

void DynamicIfTest::DeleteInterface(wlan_info_mac_role_t role) {
  uint32_t iface_id;
  zx_status_t status;

  if (role == WLAN_INFO_MAC_ROLE_CLIENT)
    iface_id = client_ifc_->iface_id_;
  else
    iface_id = softap_ifc_->iface_id_;
  status = device_->WlanphyImplDestroyIface(iface_id);
  ASSERT_EQ(status, ZX_OK);
}

void DynamicIfTest::Query(wlanphy_impl_info_t* out_info) {
  zx_status_t status;
  status = device_->WlanphyImplQuery(out_info);
  ASSERT_EQ(status, ZX_OK);
}

uint32_t DynamicIfTest::DeviceCount() { return (dev_mgr_->DevicesCount()); }

zx_status_t DynamicIfTest::StartSoftAP() {
  wlanif_start_req_t start_req = {
      .ssid = {.len = 6, .data = "Sim_AP"},
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
      .beacon_period = 100,
      .dtim_period = 100,
      .channel = kDefaultCh,
  };
  softap_ifc_->if_impl_ops_->start_req(softap_ifc_->if_impl_ctx_, &start_req);
  return ZX_OK;
}

void DynamicIfTest::ReceiveNotification(void* payload) {
  auto fn = static_cast<std::function<void()>*>(payload);
  (*fn)();
  delete fn;
}

void DynamicIfTest::Rx(const simulation::SimFrame* frame, simulation::WlanRxInfo& info) {
  ASSERT_EQ(frame->FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);

  auto mgmt_frame = static_cast<const simulation::SimManagementFrame*>(frame);
  if (mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_ASSOC_RESP) {
    auto assoc_resp = static_cast<const simulation::SimAssocRespFrame*>(mgmt_frame);
    AssocRespInfo resp_info = {.channel = info.channel,
                               .src = assoc_resp->src_addr_,
                               .dst = assoc_resp->dst_addr_,
                               .status = assoc_resp->status_};
    assoc_responses_.push_back(resp_info);
  }
}

// Create our device instance and hook up the callbacks
void DynamicIfTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  context_.assoc_resp_count = 0;
  ScheduleTestEnd(kTestDuration);
}

void DynamicIfTest::Finish() {
  for (auto ap : aps_) {
    ap->DisableBeacon();
  }
  aps_.clear();
}

void DynamicIfTest::ScheduleTestEnd(zx::duration when) {
  auto end_test_fn = new std::function<void()>;
  *end_test_fn = std::bind(&DynamicIfTest::Finish, this);
  env_->ScheduleNotification(this, when, end_test_fn);
}

void DynamicIfTest::OnJoinConf(const wlanif_join_confirm_t* resp) {
  // Send auth request
  wlanif_auth_req_t auth_req;
  std::memcpy(auth_req.peer_sta_address, context_.bssid.byte, ETH_ALEN);
  auth_req.auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM;
  auth_req.auth_failure_timeout = 1000;  // ~1s (although value is ignored for now)
  client_ifc_->if_impl_ops_->auth_req(client_ifc_->if_impl_ctx_, &auth_req);
}

void DynamicIfTest::OnAuthConf(const wlanif_auth_confirm_t* resp) {
  // Send assoc request
  wlanif_assoc_req_t assoc_req = {.rsne_len = 0, .vendor_ie_len = 0};
  memcpy(assoc_req.peer_sta_address, context_.bssid.byte, ETH_ALEN);
  client_ifc_->if_impl_ops_->assoc_req(client_ifc_->if_impl_ctx_, &assoc_req);
}

void DynamicIfTest::OnAssocConf(const wlanif_assoc_confirm_t* resp) {
  context_.assoc_resp_count++;
  EXPECT_EQ(resp->result_code, context_.expected_results.front());
  context_.expected_results.pop_front();
}

void DynamicIfTest::StartAssoc() {
  // Send join request
  wlanif_join_req join_req = {};
  std::memcpy(join_req.selected_bss.bssid, context_.bssid.byte, ETH_ALEN);
  join_req.selected_bss.ssid.len = context_.ssid.len;
  memcpy(join_req.selected_bss.ssid.data, context_.ssid.ssid, WLAN_MAX_SSID_LEN);
  join_req.selected_bss.chan = context_.channel;
  client_ifc_->if_impl_ops_->join_req(client_ifc_->if_impl_ctx_, &join_req);
}

void DynamicIfTest::ScheduleAssocReq(zx::duration when) {
  auto start_assoc_fn = new std::function<void()>;
  *start_assoc_fn = std::bind(&DynamicIfTest::StartAssoc, this);
  env_->ScheduleNotification(this, when, start_assoc_fn);
}

void DynamicIfTest::ScheduleAssocWithSoftAP(zx::duration when) {
  auto start_assoc_fn = new std::function<void()>;
  *start_assoc_fn = std::bind(&DynamicIfTest::TxAssocReq, this);
  env_->ScheduleNotification(this, when, start_assoc_fn);
}

void DynamicIfTest::TxAssocReq() {
  // Get the mac address of the SoftAP
  uint8_t mac_buf[ETH_ALEN];
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->IovarsGet(softap_ifc_->iface_id_, "cur_etheraddr", mac_buf, ETH_ALEN);
  common::MacAddr soft_ap_mac(mac_buf);
  const common::MacAddr mac(kFakeMac);
  simulation::SimAssocReqFrame assoc_req_frame(mac, soft_ap_mac);
  env_->Tx(&assoc_req_frame, kDefaultTxInfo, this);
}

void DynamicIfTest::OnAssocInd(const wlanif_assoc_ind_t* ind) {
  ASSERT_EQ(std::memcmp(ind->peer_sta_address, kFakeMac.byte, ETH_ALEN), 0);
  assoc_ind_recv_ = true;
}

void DynamicIfTest::OnAuthInd(const wlanif_auth_ind_t* ind) {
  ASSERT_EQ(std::memcmp(ind->peer_sta_address, kFakeMac.byte, ETH_ALEN), 0);
  auth_ind_recv_ = true;
}

void DynamicIfTest::VerifyAssoc() {
  // Verify the event indications were received and
  // the number of clients
  ASSERT_EQ(assoc_ind_recv_, true);
  ASSERT_EQ(auth_ind_recv_, true);
  brcmf_simdev* sim = device_->GetSim();
  uint16_t num_clients = sim->sim_fw->GetNumClients(softap_ifc_->iface_id_);
  ASSERT_EQ(num_clients, 1U);
}

void DynamicIfTest::SetChanspec(bool is_ap_iface, uint16_t* chanspec, zx_status_t expect_result) {
  brcmf_simdev* sim = device_->GetSim();
  zx_status_t err =
      sim->sim_fw->IovarsSet(is_ap_iface ? softap_ifc_->iface_id_ : client_ifc_->iface_id_,
                             "chanspec", chanspec, sizeof(uint16_t));
  EXPECT_EQ(err, expect_result);
}

uint16_t DynamicIfTest::GetChanspec(bool is_ap_iface, zx_status_t expect_result) {
  brcmf_simdev* sim = device_->GetSim();
  uint16_t chanspec;
  zx_status_t err =
      sim->sim_fw->IovarsGet(is_ap_iface ? softap_ifc_->iface_id_ : client_ifc_->iface_id_,
                             "chanspec", &chanspec, sizeof(uint16_t));
  EXPECT_EQ(err, expect_result);
  return chanspec;
}

TEST_F(DynamicIfTest, CreateDestroy) {
  Init();
  CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT);
  DeleteInterface(WLAN_INFO_MAC_ROLE_CLIENT);
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));

  CreateInterface(WLAN_INFO_MAC_ROLE_AP);
  DeleteInterface(WLAN_INFO_MAC_ROLE_AP);
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));
}

TEST_F(DynamicIfTest, DualInterfaces) {
  Init();
  CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT);
  CreateInterface(WLAN_INFO_MAC_ROLE_AP);
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(3));

  DeleteInterface(WLAN_INFO_MAC_ROLE_CLIENT);
  DeleteInterface(WLAN_INFO_MAC_ROLE_AP);
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));
}

TEST_F(DynamicIfTest, QueryInfo) {
  Init();
  CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT);
  wlanphy_impl_info_t info = {};
  // Test brcmfmac supports simutaneous client ap operation
  Query(&info);
  EXPECT_NE(info.wlan_info.caps & WLAN_INFO_HARDWARE_CAPABILITY_SIMULTANEOUS_CLIENT_AP,
            static_cast<size_t>(0));
}

// Start both client and SoftAP interfaces simultaneously and check if
// the client can associate to a FakeAP and a fake client can associate to the
// SoftAP.
TEST_F(DynamicIfTest, ConnectBothInterfaces) {
  // Create our device instances
  Init();
  CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT);
  CreateInterface(WLAN_INFO_MAC_ROLE_AP);

  // Start our SoftAP
  StartSoftAP();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));
  aps_.push_back(&ap);

  context_.expected_results.push_front(WLAN_ASSOC_RESULT_SUCCESS);

  // Associate to FakeAp
  ScheduleAssocReq(zx::msec(10));
  // Associate to SoftAP
  ScheduleAssocWithSoftAP(zx::msec(100));

  env_->Run();

  // Check if the client's assoc with FakeAP succeeded
  EXPECT_EQ(context_.assoc_resp_count, 1U);
  // Verify Assoc with SoftAP succeeded
  VerifyAssoc();
  // TODO(karthikrish) Will add disassoc once support in SIM FW is available
}

TEST_F(DynamicIfTest, SetClientChanspecAfterAPStarted) {
  // Create our device instances
  Init();

  uint16_t chanspec;
  // Create softAP iface and start
  CreateInterface(WLAN_INFO_MAC_ROLE_AP);
  StartSoftAP();

  // The chanspec of softAP iface should be set to default one.
  chanspec = GetChanspec(true, ZX_OK);
  EXPECT_EQ(chanspec, kDefaultChanpsec);

  // After creating client iface and setting a different chanspec to it, chanspec of softAP will
  // change as a result of this operation.
  CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT);
  chanspec = 1;
  SetChanspec(false, &chanspec, ZX_OK);
  chanspec = GetChanspec(true, ZX_OK);
  EXPECT_EQ(chanspec, (uint16_t)1);
}

TEST_F(DynamicIfTest, SetAPChanspecAfterClientCreated) {
  // Create our device instances
  Init();

  // Create client iface and set chanspec
  uint16_t chanspec = 1;
  CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT);
  SetChanspec(false, &chanspec, ZX_OK);

  // Create and start softAP iface to and set another chanspec
  CreateInterface(WLAN_INFO_MAC_ROLE_AP);
  StartSoftAP();
  // When we call StartSoftAP, the kDefaultCh will be transformed into chanspec(in this case the
  // value is 53397) and set to soffAP iface, but since there is already a client iface activated,
  // that input chanspec will be ignored and set to 1, which is the same as client's chanspec.
  chanspec = GetChanspec(true, ZX_OK);
  EXPECT_EQ(chanspec, (uint16_t)1);

  // Now if we set chanspec again to softAP when it already have a chanspec, this the operation will
  // be rejected.
  chanspec = 2;
  SetChanspec(true, &chanspec, ZX_ERR_BAD_STATE);
}

}  // namespace wlan::brcmfmac
