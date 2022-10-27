// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/wlan/associnfo/c/banjo.h>
#include <fuchsia/hardware/wlan/fullmac/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <fuchsia/wlan/internal/c/banjo.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <zircon/errors.h>

#include <wifi/wifi-config.h>
#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/test/device_inspect_test_utils.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/macaddr.h"

namespace wlan::brcmfmac {

namespace {

constexpr zx::duration kSimulatedClockDuration = zx::sec(10);

}  // namespace

namespace wlan_ieee80211 = ::fuchsia::wlan::ieee80211;

constexpr uint16_t kDefaultCh = 149;
constexpr wlan_channel_t kDefaultChannel = {
    .primary = kDefaultCh, .cbw = CHANNEL_BANDWIDTH_CBW20, .secondary80 = 0};
const common::MacAddr kFakeMac({0xde, 0xad, 0xbe, 0xef, 0x00, 0x02});
constexpr cssid_t kDefaultSsid = {.len = 6, .data = "Sim_AP"};

class CreateSoftAPTest : public SimTest {
 public:
  CreateSoftAPTest() = default;
  void Init();
  void CreateInterface();
  void DeleteInterface();
  zx_status_t StartSoftAP();
  zx_status_t StopSoftAP();
  uint32_t DeviceCountByProtocolId(uint32_t proto_id);

  // We track a specific firmware error condition seen in AP start.
  void GetApSetSsidErrInspectCount(uint64_t* out_count);

  void TxAuthReq(simulation::SimAuthType auth_type, common::MacAddr client_mac);
  void TxAssocReq(common::MacAddr client_mac);
  void TxDisassocReq(common::MacAddr client_mac);
  void TxDeauthReq(common::MacAddr client_mac);
  void DeauthClient(common::MacAddr client_mac);
  void VerifyAuth();
  void VerifyAssoc();
  void VerifyNotAssoc();
  void VerifyStartAPConf(uint8_t status);
  void VerifyStopAPConf(uint8_t status);
  void VerifyNumOfClient(uint16_t expect_client_num);
  void ClearAssocInd();
  void InjectStartAPError();
  void InjectChanspecError();
  void InjectSetSsidError();
  void InjectStopAPError();
  void SetExpectMacForInds(common::MacAddr set_mac);

  // Status field in the last received authentication frame.
  wlan_ieee80211::StatusCode auth_resp_status_;

  bool auth_ind_recv_ = false;
  bool assoc_ind_recv_ = false;
  bool deauth_conf_recv_ = false;
  bool deauth_ind_recv_ = false;
  bool disassoc_ind_recv_ = false;
  bool disassoc_conf_recv_ = false;
  bool start_conf_received_ = false;
  bool stop_conf_received_ = false;
  uint8_t start_conf_status_;
  uint8_t stop_conf_status_;

  // The expect mac address for indications
  common::MacAddr ind_expect_mac_ = kFakeMac;

 protected:
  simulation::WlanTxInfo tx_info_ = {.channel = kDefaultChannel};
  bool sec_enabled_ = false;

 private:
  void Rx(std::shared_ptr<const simulation::SimFrame> frame,
          std::shared_ptr<const simulation::WlanRxInfo> info) override;
  // SME callbacks
  static wlan_fullmac_impl_ifc_protocol_ops_t sme_ops_;
  wlan_fullmac_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};
  SimInterface softap_ifc_;

  void OnAuthInd(const wlan_fullmac_auth_ind_t* ind);
  void OnDeauthInd(const wlan_fullmac_deauth_indication_t* ind);
  void OnDeauthConf(const wlan_fullmac_deauth_confirm_t* resp);
  void OnAssocInd(const wlan_fullmac_assoc_ind_t* ind);
  void OnDisassocConf(const wlan_fullmac_disassoc_confirm_t* resp);
  void OnDisassocInd(const wlan_fullmac_disassoc_indication_t* ind);
  void OnStartConf(const wlan_fullmac_start_confirm_t* resp);
  void OnStopConf(const wlan_fullmac_stop_confirm_t* resp);
  void OnChannelSwitch(const wlan_fullmac_channel_switch_info_t* info);
  uint16_t CreateRsneIe(uint8_t* buffer);
};

void CreateSoftAPTest::Rx(std::shared_ptr<const simulation::SimFrame> frame,
                          std::shared_ptr<const simulation::WlanRxInfo> info) {
  ASSERT_EQ(frame->FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);

  auto mgmt_frame = std::static_pointer_cast<const simulation::SimManagementFrame>(frame);
  if (mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_AUTH) {
    auto auth_frame = std::static_pointer_cast<const simulation::SimAuthFrame>(mgmt_frame);
    if (auth_frame->seq_num_ == 2)
      auth_resp_status_ = auth_frame->status_;
  }
}

wlan_fullmac_impl_ifc_protocol_ops_t CreateSoftAPTest::sme_ops_ = {
    // SME operations
    .auth_ind =
        [](void* cookie, const wlan_fullmac_auth_ind_t* ind) {
          static_cast<CreateSoftAPTest*>(cookie)->OnAuthInd(ind);
        },
    .deauth_conf =
        [](void* cookie, const wlan_fullmac_deauth_confirm_t* resp) {
          static_cast<CreateSoftAPTest*>(cookie)->OnDeauthConf(resp);
        },
    .deauth_ind =
        [](void* cookie, const wlan_fullmac_deauth_indication_t* ind) {
          static_cast<CreateSoftAPTest*>(cookie)->OnDeauthInd(ind);
        },
    .assoc_ind =
        [](void* cookie, const wlan_fullmac_assoc_ind_t* ind) {
          static_cast<CreateSoftAPTest*>(cookie)->OnAssocInd(ind);
        },
    .disassoc_conf =
        [](void* cookie, const wlan_fullmac_disassoc_confirm_t* resp) {
          static_cast<CreateSoftAPTest*>(cookie)->OnDisassocConf(resp);
        },
    .disassoc_ind =
        [](void* cookie, const wlan_fullmac_disassoc_indication_t* ind) {
          static_cast<CreateSoftAPTest*>(cookie)->OnDisassocInd(ind);
        },
    .start_conf =
        [](void* cookie, const wlan_fullmac_start_confirm_t* resp) {
          static_cast<CreateSoftAPTest*>(cookie)->OnStartConf(resp);
        },
    .stop_conf =
        [](void* cookie, const wlan_fullmac_stop_confirm_t* resp) {
          static_cast<CreateSoftAPTest*>(cookie)->OnStopConf(resp);
        },
    .on_channel_switch =
        [](void* cookie, const wlan_fullmac_channel_switch_info_t* info) {
          static_cast<CreateSoftAPTest*>(cookie)->OnChannelSwitch(info);
        },
};

void CreateSoftAPTest::Init() { ASSERT_EQ(SimTest::Init(), ZX_OK); }

void CreateSoftAPTest::CreateInterface() {
  zx_status_t status;

  status = SimTest::StartInterface(WLAN_MAC_ROLE_AP, &softap_ifc_, &sme_protocol_);
  ASSERT_EQ(status, ZX_OK);
}

void CreateSoftAPTest::DeleteInterface() {
  EXPECT_EQ(SimTest::DeleteInterface(&softap_ifc_), ZX_OK);
}

uint32_t CreateSoftAPTest::DeviceCountByProtocolId(uint32_t proto_id) {
  return dev_mgr_->DeviceCountByProtocolId(proto_id);
}

void CreateSoftAPTest::GetApSetSsidErrInspectCount(uint64_t* out_count) {
  ASSERT_NOT_NULL(out_count);
  auto hierarchy = FetchHierarchy(device_->GetInspect()->inspector());
  auto* root = hierarchy.value().GetByPath({"brcmfmac-phy"});
  ASSERT_NOT_NULL(root);
  // Only verify the value of hourly counter here, the relationship between hourly counter and daily
  // counter is verified in device_inspect_test.
  auto* uint_property = root->node().get_property<inspect::UintPropertyValue>("ap_set_ssid_err");
  ASSERT_NOT_NULL(uint_property);
  *out_count = uint_property->value();
}

uint16_t CreateSoftAPTest::CreateRsneIe(uint8_t* buffer) {
  // construct a fake rsne ie in the input buffer
  uint16_t offset = 0;
  uint8_t* ie = buffer;

  ie[offset++] = WLAN_IE_TYPE_RSNE;
  ie[offset++] = 20;  // The length of following content.

  // These two bytes are 16-bit version number.
  ie[offset++] = 1;  // Lower byte
  ie[offset++] = 0;  // Higher byte

  memcpy(&ie[offset], RSN_OUI,
         TLV_OUI_LEN);  // RSN OUI for multicast cipher suite.
  offset += TLV_OUI_LEN;
  ie[offset++] = WPA_CIPHER_TKIP;  // Set multicast cipher suite.

  // These two bytes indicate the length of unicast cipher list, in this case is 1.
  ie[offset++] = 1;  // Lower byte
  ie[offset++] = 0;  // Higher byte

  memcpy(&ie[offset], RSN_OUI,
         TLV_OUI_LEN);  // RSN OUI for unicast cipher suite.
  offset += TLV_OUI_LEN;
  ie[offset++] = WPA_CIPHER_CCMP_128;  // Set unicast cipher suite.

  // These two bytes indicate the length of auth management suite list, in this case is 1.
  ie[offset++] = 1;  // Lower byte
  ie[offset++] = 0;  // Higher byte

  memcpy(&ie[offset], RSN_OUI,
         TLV_OUI_LEN);  // RSN OUI for auth management suite.
  offset += TLV_OUI_LEN;
  ie[offset++] = RSN_AKM_PSK;  // Set auth management suite.

  // These two bytes indicate RSN capabilities, in this case is \x0c\x00.
  ie[offset++] = 12;  // Lower byte
  ie[offset++] = 0;   // Higher byte

  // ASSERT_EQ(offset, (const uint32_t) (ie[TLV_LEN_OFF] + TLV_HDR_LEN));
  return offset;
}

zx_status_t CreateSoftAPTest::StartSoftAP() {
  wlan_fullmac_start_req_t start_req = {
      .ssid = {.len = 6, .data = "Sim_AP"},
      .bss_type = BSS_TYPE_INFRASTRUCTURE,
      .beacon_period = 100,
      .dtim_period = 100,
      .channel = kDefaultCh,
      .rsne_len = 0,
  };
  // If sec mode is requested, create a dummy RSNE IE (our SoftAP only
  // supports WPA2)
  if (sec_enabled_ == true) {
    start_req.rsne_len = CreateRsneIe(start_req.rsne);
  }
  softap_ifc_.if_impl_ops_->start_req(softap_ifc_.if_impl_ctx_, &start_req);

  // Retrieve wsec from SIM FW to check if it is set appropriately
  brcmf_simdev* sim = device_->GetSim();
  uint32_t wsec;
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, softap_ifc_.iface_id_);
  zx_status_t status = brcmf_fil_iovar_int_get(ifp, "wsec", &wsec, nullptr);
  EXPECT_EQ(status, ZX_OK);
  if (sec_enabled_ == true)
    EXPECT_NE(wsec, (uint32_t)0);
  else
    EXPECT_EQ(wsec, (uint32_t)0);
  return ZX_OK;
}

void CreateSoftAPTest::InjectStartAPError() {
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjCmd(BRCMF_C_SET_SSID, ZX_ERR_IO, BCME_OK, softap_ifc_.iface_id_);
}

void CreateSoftAPTest::InjectStopAPError() {
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjIovar("bss", ZX_ERR_IO, BCME_OK, softap_ifc_.iface_id_);
}

void CreateSoftAPTest::InjectChanspecError() {
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjIovar("chanspec", ZX_ERR_IO, BCME_BADARG, softap_ifc_.iface_id_);
}

void CreateSoftAPTest::InjectSetSsidError() {
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjCmd(BRCMF_C_SET_SSID, ZX_OK, BCME_ERROR, softap_ifc_.iface_id_);
}

void CreateSoftAPTest::SetExpectMacForInds(common::MacAddr set_mac) { ind_expect_mac_ = set_mac; }

zx_status_t CreateSoftAPTest::StopSoftAP() {
  wlan_fullmac_stop_req_t stop_req{
      .ssid = {.len = 6, .data = "Sim_AP"},
  };
  softap_ifc_.if_impl_ops_->stop_req(softap_ifc_.if_impl_ctx_, &stop_req);
  return ZX_OK;
}

void CreateSoftAPTest::OnAuthInd(const wlan_fullmac_auth_ind_t* ind) {
  ASSERT_EQ(std::memcmp(ind->peer_sta_address, ind_expect_mac_.byte, ETH_ALEN), 0);
  auth_ind_recv_ = true;
}
void CreateSoftAPTest::OnDeauthInd(const wlan_fullmac_deauth_indication_t* ind) {
  ASSERT_EQ(std::memcmp(ind->peer_sta_address, ind_expect_mac_.byte, ETH_ALEN), 0);
  deauth_ind_recv_ = true;
}
void CreateSoftAPTest::OnDeauthConf(const wlan_fullmac_deauth_confirm_t* resp) {
  ASSERT_EQ(std::memcmp(resp->peer_sta_address, ind_expect_mac_.byte, ETH_ALEN), 0);
  deauth_conf_recv_ = true;
}
void CreateSoftAPTest::OnAssocInd(const wlan_fullmac_assoc_ind_t* ind) {
  ASSERT_EQ(std::memcmp(ind->peer_sta_address, ind_expect_mac_.byte, ETH_ALEN), 0);
  assoc_ind_recv_ = true;
}
void CreateSoftAPTest::OnDisassocConf(const wlan_fullmac_disassoc_confirm_t* resp) {
  disassoc_conf_recv_ = true;
}
void CreateSoftAPTest::OnDisassocInd(const wlan_fullmac_disassoc_indication_t* ind) {
  ASSERT_EQ(std::memcmp(ind->peer_sta_address, ind_expect_mac_.byte, ETH_ALEN), 0);
  disassoc_ind_recv_ = true;
}

void CreateSoftAPTest::OnStartConf(const wlan_fullmac_start_confirm_t* resp) {
  start_conf_received_ = true;
  start_conf_status_ = resp->result_code;
}

void CreateSoftAPTest::OnStopConf(const wlan_fullmac_stop_confirm_t* resp) {
  stop_conf_received_ = true;
  stop_conf_status_ = resp->result_code;
}

void CreateSoftAPTest::OnChannelSwitch(const wlan_fullmac_channel_switch_info_t* info) {}

void CreateSoftAPTest::TxAssocReq(common::MacAddr client_mac) {
  // Get the mac address of the SoftAP
  common::MacAddr soft_ap_mac;
  softap_ifc_.GetMacAddr(&soft_ap_mac);
  simulation::SimAssocReqFrame assoc_req_frame(client_mac, soft_ap_mac, kDefaultSsid);
  env_->Tx(assoc_req_frame, tx_info_, this);
}

void CreateSoftAPTest::TxAuthReq(simulation::SimAuthType auth_type, common::MacAddr client_mac) {
  // Get the mac address of the SoftAP
  common::MacAddr soft_ap_mac;
  softap_ifc_.GetMacAddr(&soft_ap_mac);
  simulation::SimAuthFrame auth_req_frame(client_mac, soft_ap_mac, 1, auth_type,
                                          wlan_ieee80211::StatusCode::SUCCESS);
  env_->Tx(auth_req_frame, tx_info_, this);
}

void CreateSoftAPTest::TxDisassocReq(common::MacAddr client_mac) {
  // Get the mac address of the SoftAP
  common::MacAddr soft_ap_mac;
  softap_ifc_.GetMacAddr(&soft_ap_mac);
  // Disassociate with the SoftAP
  simulation::SimDisassocReqFrame disassoc_req_frame(
      client_mac, soft_ap_mac, wlan_ieee80211::ReasonCode::LEAVING_NETWORK_DISASSOC);
  env_->Tx(disassoc_req_frame, tx_info_, this);
}

void CreateSoftAPTest::TxDeauthReq(common::MacAddr client_mac) {
  // Get the mac address of the SoftAP
  common::MacAddr soft_ap_mac;
  softap_ifc_.GetMacAddr(&soft_ap_mac);
  // Disassociate with the SoftAP
  simulation::SimDeauthFrame deauth_frame(client_mac, soft_ap_mac,
                                          wlan_ieee80211::ReasonCode::LEAVING_NETWORK_DEAUTH);
  env_->Tx(deauth_frame, tx_info_, this);
}

void CreateSoftAPTest::DeauthClient(common::MacAddr client_mac) {
  wlan_fullmac_deauth_req_t req;

  memcpy(req.peer_sta_address, client_mac.byte, ETH_ALEN);
  req.reason_code = 0;

  softap_ifc_.if_impl_ops_->deauth_req(softap_ifc_.if_impl_ctx_, &req);
}

void CreateSoftAPTest::VerifyAuth() {
  ASSERT_EQ(auth_ind_recv_, true);
  // When auth is done, the client is already added into client list.
  VerifyNumOfClient(1);
}

void CreateSoftAPTest::VerifyAssoc() {
  // Verify the event indications were received and
  // the number of clients
  ASSERT_EQ(auth_ind_recv_, true);
  ASSERT_EQ(assoc_ind_recv_, true);
  VerifyNumOfClient(1);
}

void CreateSoftAPTest::ClearAssocInd() { assoc_ind_recv_ = false; }

void CreateSoftAPTest::VerifyNumOfClient(uint16_t expect_client_num) {
  brcmf_simdev* sim = device_->GetSim();
  uint16_t num_clients = sim->sim_fw->GetNumClients(softap_ifc_.iface_id_);
  ASSERT_EQ(num_clients, expect_client_num);
}

void CreateSoftAPTest::VerifyNotAssoc() {
  ASSERT_EQ(assoc_ind_recv_, false);
  ASSERT_EQ(auth_ind_recv_, false);
  VerifyNumOfClient(0);
}

void CreateSoftAPTest::VerifyStartAPConf(uint8_t status) {
  ASSERT_EQ(start_conf_received_, true);
  ASSERT_EQ(start_conf_status_, status);
}

void CreateSoftAPTest::VerifyStopAPConf(uint8_t status) {
  ASSERT_EQ(stop_conf_received_, true);
  ASSERT_EQ(stop_conf_status_, status);
}

TEST_F(CreateSoftAPTest, SetDefault) {
  Init();
  CreateInterface();
  DeleteInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 0u);
}

TEST_F(CreateSoftAPTest, CreateSoftAP) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);
  zx::duration delay = zx::msec(10);
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::StartSoftAP, this), delay);
  delay += kStartAPLinkEventDelay + kApStartedEventDelay + zx::msec(10);
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::StopSoftAP, this), delay);
  env_->Run(kSimulatedClockDuration);
  VerifyStartAPConf(WLAN_START_RESULT_SUCCESS);
}

TEST_F(CreateSoftAPTest, CreateSoftAPFail) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);
  InjectStartAPError();
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::StartSoftAP, this), zx::msec(50));
  env_->Run(kSimulatedClockDuration);
  VerifyStartAPConf(WLAN_START_RESULT_NOT_SUPPORTED);
}

TEST_F(CreateSoftAPTest, CreateSoftAPFail_ChanSetError) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);
  InjectChanspecError();
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::StartSoftAP, this), zx::msec(50));
  env_->Run(kSimulatedClockDuration);
  VerifyStartAPConf(WLAN_START_RESULT_NOT_SUPPORTED);
}

// SoftAP can encounter this specific SET_SSID firmware error, which we detect and log.
TEST_F(CreateSoftAPTest, CreateSoftAPFail_SetSsidError) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);
  InjectSetSsidError();
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::StartSoftAP, this), zx::msec(50));
  uint64_t count;
  GetApSetSsidErrInspectCount(&count);
  ASSERT_EQ(count, 0u);
  env_->Run(kSimulatedClockDuration);

  VerifyStartAPConf(WLAN_START_RESULT_NOT_SUPPORTED);

  // Verify inspect is updated.
  GetApSetSsidErrInspectCount(&count);
  EXPECT_EQ(count, 1u);
}

// Fail the iovar bss but Stop AP should still succeed
TEST_F(CreateSoftAPTest, BssIovarFail) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);
  InjectStopAPError();
  // Start SoftAP
  StartSoftAP();
  StopSoftAP();
  VerifyStopAPConf(WLAN_START_RESULT_SUCCESS);
}
// Start SoftAP in secure mode and then restart in open mode.
// Appropriate secure mode is checked in StartSoftAP() after SoftAP
// is started
TEST_F(CreateSoftAPTest, CreateSecureSoftAP) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);
  // Start SoftAP in secure mode
  sec_enabled_ = true;
  StartSoftAP();
  StopSoftAP();
  // Restart SoftAP in open mode
  StartSoftAP();
  StopSoftAP();
}

TEST_F(CreateSoftAPTest, AssociateWithSoftAP) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);
  StartSoftAP();
  env_->ScheduleNotification(
      std::bind(&CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN, kFakeMac),
      zx::msec(5));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::VerifyAuth, this), zx::msec(8));

  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::TxAssocReq, this, kFakeMac),
                             zx::msec(10));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::VerifyAssoc, this), zx::msec(50));
  env_->Run(kSimulatedClockDuration);
}

TEST_F(CreateSoftAPTest, DisassociateThenAssociateWithSoftAP) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);
  StartSoftAP();
  env_->ScheduleNotification(
      std::bind(&CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN, kFakeMac),
      zx::msec(5));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::VerifyAuth, this), zx::msec(8));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::TxAssocReq, this, kFakeMac),
                             zx::msec(10));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::VerifyAssoc, this), zx::msec(50));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::ClearAssocInd, this), zx::msec(75));
  // Assoc a second time
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::TxAssocReq, this, kFakeMac),
                             zx::msec(100));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::VerifyAssoc, this), zx::msec(150));
  env_->Run(kSimulatedClockDuration);
}

TEST_F(CreateSoftAPTest, DisassociateFromSoftAP) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);
  StartSoftAP();
  env_->ScheduleNotification(
      std::bind(&CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN, kFakeMac),
      zx::msec(5));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::VerifyAuth, this), zx::msec(8));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::TxAssocReq, this, kFakeMac),
                             zx::msec(10));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::VerifyAssoc, this), zx::msec(50));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::TxDisassocReq, this, kFakeMac),
                             zx::msec(60));
  env_->Run(kSimulatedClockDuration);
  // Only disassoc ind should be seen.
  EXPECT_EQ(deauth_ind_recv_, false);
  EXPECT_EQ(disassoc_ind_recv_, true);
  VerifyNumOfClient(0);
}

// After a client associates, deauth it from the SoftAP itself.
TEST_F(CreateSoftAPTest, DisassociateClientFromSoftAP) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);
  StartSoftAP();
  env_->ScheduleNotification(
      std::bind(&CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN, kFakeMac),
      zx::msec(5));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::VerifyAuth, this), zx::msec(8));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::TxAssocReq, this, kFakeMac),
                             zx::msec(10));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::VerifyAssoc, this), zx::msec(50));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::DeauthClient, this, kFakeMac),
                             zx::msec(60));
  env_->Run(kSimulatedClockDuration);
  // Should have received disassoc conf.
  EXPECT_EQ(disassoc_conf_recv_, true);
  VerifyNumOfClient(0);
}

TEST_F(CreateSoftAPTest, AssocWithWrongAuth) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);
  StartSoftAP();
  env_->ScheduleNotification(
      std::bind(&CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_SHARED_KEY, kFakeMac),
      zx::msec(5));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::TxAssocReq, this, kFakeMac),
                             zx::msec(10));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::VerifyNotAssoc, this), zx::msec(20));
  env_->Run(kSimulatedClockDuration);
  EXPECT_EQ(auth_resp_status_, wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED);
}

TEST_F(CreateSoftAPTest, DeauthBeforeAssoc) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);
  StartSoftAP();
  env_->ScheduleNotification(
      std::bind(&CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN, kFakeMac),
      zx::msec(5));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::VerifyAuth, this), zx::msec(10));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::TxDeauthReq, this, kFakeMac),
                             zx::msec(20));
  env_->Run(kSimulatedClockDuration);
  // Only deauth ind shoulb be seen.
  EXPECT_EQ(deauth_ind_recv_, true);
  EXPECT_EQ(disassoc_ind_recv_, false);
  VerifyNumOfClient(0);
}

TEST_F(CreateSoftAPTest, DeauthWhileAssociated) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);
  StartSoftAP();
  env_->ScheduleNotification(
      std::bind(&CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN, kFakeMac),
      zx::msec(5));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::VerifyAuth, this), zx::msec(8));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::TxAssocReq, this, kFakeMac),
                             zx::msec(10));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::VerifyAssoc, this), zx::msec(50));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::TxDeauthReq, this, kFakeMac),
                             zx::msec(60));
  env_->Run(kSimulatedClockDuration);
  // Both indication should be seen.
  EXPECT_EQ(deauth_ind_recv_, true);
  EXPECT_EQ(disassoc_ind_recv_, true);
  VerifyNumOfClient(0);
}

const common::MacAddr kSecondClientMac({0xde, 0xad, 0xbe, 0xef, 0x00, 0x04});

TEST_F(CreateSoftAPTest, DeauthMultiClients) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCountByProtocolId(ZX_PROTOCOL_WLAN_FULLMAC_IMPL), 1u);
  StartSoftAP();
  env_->ScheduleNotification(
      std::bind(&CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN, kFakeMac),
      zx::msec(5));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::TxAssocReq, this, kFakeMac),
                             zx::msec(10));
  env_->ScheduleNotification(
      std::bind(&CreateSoftAPTest::SetExpectMacForInds, this, kSecondClientMac), zx::msec(15));
  env_->ScheduleNotification(
      std::bind(&CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN, kSecondClientMac),
      zx::msec(20));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::TxAssocReq, this, kSecondClientMac),
                             zx::msec(30));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::VerifyNumOfClient, this, 2),
                             zx::msec(40));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::SetExpectMacForInds, this, kFakeMac),
                             zx::msec(45));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::TxDeauthReq, this, kFakeMac),
                             zx::msec(50));
  env_->ScheduleNotification(std::bind(&CreateSoftAPTest::VerifyNumOfClient, this, 1),
                             zx::msec(60));
  env_->Run(kSimulatedClockDuration);
}

}  // namespace wlan::brcmfmac
