// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wifi/wifi-config.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

constexpr uint16_t kDefaultCh = 149;
constexpr wlan_channel_t kDefaultChannel = {
    .primary = kDefaultCh, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
const common::MacAddr kFakeMac({0xde, 0xad, 0xbe, 0xef, 0x00, 0x02});
constexpr wlan_ssid_t kDefaultSsid = {.len = 6, .ssid = "Sim_AP"};

class CreateSoftAPTest : public SimTest {
 public:
  CreateSoftAPTest() = default;
  void Init();
  void CreateInterface();
  void DeleteInterface();
  zx_status_t StartSoftAP();
  zx_status_t StopSoftAP();
  uint32_t DeviceCount();
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
  void InjectStopAPError();
  void SetExpectMacForInds(common::MacAddr set_mac);

  // Status field in the last received authentication frame.
  uint16_t auth_resp_status_;

  bool auth_ind_recv_ = false;
  bool assoc_ind_recv_ = false;
  bool deauth_ind_recv_ = false;
  bool disassoc_ind_recv_ = false;
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
  static wlanif_impl_ifc_protocol_ops_t sme_ops_;
  wlanif_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};
  SimInterface softap_ifc_;

  void OnAuthInd(const wlanif_auth_ind_t* ind);
  void OnDeauthInd(const wlanif_deauth_indication_t* ind);
  void OnAssocInd(const wlanif_assoc_ind_t* ind);
  void OnDisassocInd(const wlanif_disassoc_indication_t* ind);
  void OnStartConf(const wlanif_start_confirm_t* resp);
  void OnStopConf(const wlanif_stop_confirm_t* resp);
  void OnChannelSwitch(const wlanif_channel_switch_info_t* info);
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

wlanif_impl_ifc_protocol_ops_t CreateSoftAPTest::sme_ops_ = {
    // SME operations
    .auth_ind =
        [](void* cookie, const wlanif_auth_ind_t* ind) {
          static_cast<CreateSoftAPTest*>(cookie)->OnAuthInd(ind);
        },
    .deauth_ind =
        [](void* cookie, const wlanif_deauth_indication_t* ind) {
          static_cast<CreateSoftAPTest*>(cookie)->OnDeauthInd(ind);
        },
    .assoc_ind =
        [](void* cookie, const wlanif_assoc_ind_t* ind) {
          static_cast<CreateSoftAPTest*>(cookie)->OnAssocInd(ind);
        },
    .disassoc_ind =
        [](void* cookie, const wlanif_disassoc_indication_t* ind) {
          static_cast<CreateSoftAPTest*>(cookie)->OnDisassocInd(ind);
        },
    .start_conf =
        [](void* cookie, const wlanif_start_confirm_t* resp) {
          static_cast<CreateSoftAPTest*>(cookie)->OnStartConf(resp);
        },
    .stop_conf =
        [](void* cookie, const wlanif_stop_confirm_t* resp) {
          static_cast<CreateSoftAPTest*>(cookie)->OnStopConf(resp);
        },
    .on_channel_switch =
        [](void* cookie, const wlanif_channel_switch_info_t* info) {
          static_cast<CreateSoftAPTest*>(cookie)->OnChannelSwitch(info);
        },
};

void CreateSoftAPTest::Init() { ASSERT_EQ(SimTest::Init(), ZX_OK); }

void CreateSoftAPTest::CreateInterface() {
  zx_status_t status;

  status = SimTest::StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_, &sme_protocol_);
  ASSERT_EQ(status, ZX_OK);
}

void CreateSoftAPTest::DeleteInterface() { SimTest::DeleteInterface(softap_ifc_.iface_id_); }

uint32_t CreateSoftAPTest::DeviceCount() { return (dev_mgr_->DeviceCount()); }

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
  wlanif_start_req_t start_req = {
      .ssid = {.len = 6, .data = "Sim_AP"},
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
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
  sim->sim_fw->err_inj_.AddErrInjCmd(BRCMF_C_SET_SSID, ZX_ERR_IO, softap_ifc_.iface_id_);
}

void CreateSoftAPTest::InjectStopAPError() {
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjIovar("bss", ZX_ERR_IO, softap_ifc_.iface_id_);
}

void CreateSoftAPTest::SetExpectMacForInds(common::MacAddr set_mac) { ind_expect_mac_ = set_mac; }

zx_status_t CreateSoftAPTest::StopSoftAP() {
  wlanif_stop_req_t stop_req{
      .ssid = {.len = 6, .data = "Sim_AP"},
  };
  softap_ifc_.if_impl_ops_->stop_req(softap_ifc_.if_impl_ctx_, &stop_req);
  return ZX_OK;
}

void CreateSoftAPTest::OnAuthInd(const wlanif_auth_ind_t* ind) {
  ASSERT_EQ(std::memcmp(ind->peer_sta_address, ind_expect_mac_.byte, ETH_ALEN), 0);
  auth_ind_recv_ = true;
}
void CreateSoftAPTest::OnDeauthInd(const wlanif_deauth_indication_t* ind) {
  ASSERT_EQ(std::memcmp(ind->peer_sta_address, ind_expect_mac_.byte, ETH_ALEN), 0);
  deauth_ind_recv_ = true;
}
void CreateSoftAPTest::OnAssocInd(const wlanif_assoc_ind_t* ind) {
  ASSERT_EQ(std::memcmp(ind->peer_sta_address, ind_expect_mac_.byte, ETH_ALEN), 0);
  assoc_ind_recv_ = true;
}
void CreateSoftAPTest::OnDisassocInd(const wlanif_disassoc_indication_t* ind) {
  ASSERT_EQ(std::memcmp(ind->peer_sta_address, ind_expect_mac_.byte, ETH_ALEN), 0);
  disassoc_ind_recv_ = true;
}

void CreateSoftAPTest::OnStartConf(const wlanif_start_confirm_t* resp) {
  start_conf_received_ = true;
  start_conf_status_ = resp->result_code;
}

void CreateSoftAPTest::OnStopConf(const wlanif_stop_confirm_t* resp) {
  stop_conf_received_ = true;
  stop_conf_status_ = resp->result_code;
}

void CreateSoftAPTest::OnChannelSwitch(const wlanif_channel_switch_info_t* info) {}

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
                                          WLAN_STATUS_CODE_SUCCESS);
  env_->Tx(auth_req_frame, tx_info_, this);
}

void CreateSoftAPTest::TxDisassocReq(common::MacAddr client_mac) {
  // Get the mac address of the SoftAP
  common::MacAddr soft_ap_mac;
  softap_ifc_.GetMacAddr(&soft_ap_mac);
  // Disassociate with the SoftAP
  simulation::SimDisassocReqFrame disassoc_req_frame(client_mac, soft_ap_mac, 0);
  env_->Tx(disassoc_req_frame, tx_info_, this);
}

void CreateSoftAPTest::TxDeauthReq(common::MacAddr client_mac) {
  // Get the mac address of the SoftAP
  common::MacAddr soft_ap_mac;
  softap_ifc_.GetMacAddr(&soft_ap_mac);
  // Disassociate with the SoftAP
  simulation::SimDeauthFrame deauth_frame(client_mac, soft_ap_mac, 0);
  env_->Tx(deauth_frame, tx_info_, this);
}

void CreateSoftAPTest::DeauthClient(common::MacAddr client_mac) {
  wlanif_deauth_req_t req;

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
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));
}

TEST_F(CreateSoftAPTest, CreateSoftAP) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  zx::duration delay = zx::msec(10);
  SCHEDULE_CALL(delay, &CreateSoftAPTest::StartSoftAP, this);
  delay += kStartAPConfDelay + zx::msec(10);
  SCHEDULE_CALL(delay, &CreateSoftAPTest::StopSoftAP, this);
  // Wait until SoftAP start conf is received
  // SCHEDULE_CALL(delay, &CreateSoftAPTest::VerifyStartAPConf, this, WLAN_START_RESULT_SUCCESS);
  env_->Run();
  VerifyStartAPConf(WLAN_START_RESULT_SUCCESS);
}

TEST_F(CreateSoftAPTest, CreateSoftAPFail) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  InjectStartAPError();
  SCHEDULE_CALL(zx::msec(50), &CreateSoftAPTest::StartSoftAP, this);
  env_->Run();
  VerifyStartAPConf(WLAN_START_RESULT_NOT_SUPPORTED);
}

// Fail the iovar bss but Stop AP should still succeed
TEST_F(CreateSoftAPTest, BssIovarFail) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
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
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
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
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  StartSoftAP();
  SCHEDULE_CALL(zx::msec(5), &CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN,
                kFakeMac);
  SCHEDULE_CALL(zx::msec(8), &CreateSoftAPTest::VerifyAuth, this);

  SCHEDULE_CALL(zx::msec(10), &CreateSoftAPTest::TxAssocReq, this, kFakeMac);
  SCHEDULE_CALL(zx::msec(50), &CreateSoftAPTest::VerifyAssoc, this);
  env_->Run();
}

TEST_F(CreateSoftAPTest, ReassociateWithSoftAP) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  StartSoftAP();
  SCHEDULE_CALL(zx::msec(5), &CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN,
                kFakeMac);
  SCHEDULE_CALL(zx::msec(8), &CreateSoftAPTest::VerifyAuth, this);
  SCHEDULE_CALL(zx::msec(10), &CreateSoftAPTest::TxAssocReq, this, kFakeMac);
  SCHEDULE_CALL(zx::msec(50), &CreateSoftAPTest::VerifyAssoc, this);
  SCHEDULE_CALL(zx::msec(75), &CreateSoftAPTest::ClearAssocInd, this);
  // Reassoc
  SCHEDULE_CALL(zx::msec(100), &CreateSoftAPTest::TxAssocReq, this, kFakeMac);
  SCHEDULE_CALL(zx::msec(150), &CreateSoftAPTest::VerifyAssoc, this);
  env_->Run();
}

TEST_F(CreateSoftAPTest, DisassociateFromSoftAP) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  StartSoftAP();
  SCHEDULE_CALL(zx::msec(5), &CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN,
                kFakeMac);
  SCHEDULE_CALL(zx::msec(8), &CreateSoftAPTest::VerifyAuth, this);
  SCHEDULE_CALL(zx::msec(10), &CreateSoftAPTest::TxAssocReq, this, kFakeMac);
  SCHEDULE_CALL(zx::msec(50), &CreateSoftAPTest::VerifyAssoc, this);
  SCHEDULE_CALL(zx::msec(60), &CreateSoftAPTest::TxDisassocReq, this, kFakeMac);
  env_->Run();
  // Only disassoc ind should be seen.
  EXPECT_EQ(deauth_ind_recv_, false);
  EXPECT_EQ(disassoc_ind_recv_, true);
  VerifyNumOfClient(0);
}

// After a client associates, deauth it from the SoftAP itself.
TEST_F(CreateSoftAPTest, DisassociateClientFromSoftAP) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  StartSoftAP();
  SCHEDULE_CALL(zx::msec(5), &CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN,
                kFakeMac);
  SCHEDULE_CALL(zx::msec(8), &CreateSoftAPTest::VerifyAuth, this);
  SCHEDULE_CALL(zx::msec(10), &CreateSoftAPTest::TxAssocReq, this, kFakeMac);
  SCHEDULE_CALL(zx::msec(50), &CreateSoftAPTest::VerifyAssoc, this);
  SCHEDULE_CALL(zx::msec(60), &CreateSoftAPTest::DeauthClient, this, kFakeMac);
  env_->Run();
  // Should have received deauth and disassoc indications.
  EXPECT_EQ(deauth_ind_recv_, true);
  EXPECT_EQ(disassoc_ind_recv_, true);
  VerifyNumOfClient(0);
}

TEST_F(CreateSoftAPTest, AssocWithWrongAuth) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  StartSoftAP();
  SCHEDULE_CALL(zx::msec(5), &CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_SHARED_KEY,
                kFakeMac);
  SCHEDULE_CALL(zx::msec(10), &CreateSoftAPTest::TxAssocReq, this, kFakeMac);
  SCHEDULE_CALL(zx::msec(20), &CreateSoftAPTest::VerifyNotAssoc, this);
  env_->Run();
  EXPECT_EQ(auth_resp_status_, WLAN_STATUS_CODE_REFUSED);
}

TEST_F(CreateSoftAPTest, DeauthBeforeAssoc) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  StartSoftAP();
  SCHEDULE_CALL(zx::msec(5), &CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN,
                kFakeMac);
  SCHEDULE_CALL(zx::msec(10), &CreateSoftAPTest::VerifyAuth, this);
  SCHEDULE_CALL(zx::msec(20), &CreateSoftAPTest::TxDeauthReq, this, kFakeMac);
  env_->Run();
  // Only deauth ind shoulb be seen.
  EXPECT_EQ(deauth_ind_recv_, true);
  EXPECT_EQ(disassoc_ind_recv_, false);
  VerifyNumOfClient(0);
}

TEST_F(CreateSoftAPTest, DeauthWhileAssociated) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  StartSoftAP();
  SCHEDULE_CALL(zx::msec(5), &CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN,
                kFakeMac);
  SCHEDULE_CALL(zx::msec(8), &CreateSoftAPTest::VerifyAuth, this);
  SCHEDULE_CALL(zx::msec(10), &CreateSoftAPTest::TxAssocReq, this, kFakeMac);
  SCHEDULE_CALL(zx::msec(50), &CreateSoftAPTest::VerifyAssoc, this);
  SCHEDULE_CALL(zx::msec(60), &CreateSoftAPTest::TxDeauthReq, this, kFakeMac);
  env_->Run();
  // Both indication should be seen.
  EXPECT_EQ(deauth_ind_recv_, true);
  EXPECT_EQ(disassoc_ind_recv_, true);
  VerifyNumOfClient(0);
}

const common::MacAddr kSecondClientMac({0xde, 0xad, 0xbe, 0xef, 0x00, 0x04});

TEST_F(CreateSoftAPTest, DeauthMultiClients) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  StartSoftAP();
  SCHEDULE_CALL(zx::msec(5), &CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN,
                kFakeMac);
  SCHEDULE_CALL(zx::msec(10), &CreateSoftAPTest::TxAssocReq, this, kFakeMac);
  SCHEDULE_CALL(zx::msec(15), &CreateSoftAPTest::SetExpectMacForInds, this, kSecondClientMac);
  SCHEDULE_CALL(zx::msec(20), &CreateSoftAPTest::TxAuthReq, this, simulation::AUTH_TYPE_OPEN,
                kSecondClientMac);
  SCHEDULE_CALL(zx::msec(30), &CreateSoftAPTest::TxAssocReq, this, kSecondClientMac);
  SCHEDULE_CALL(zx::msec(40), &CreateSoftAPTest::VerifyNumOfClient, this, 2);
  SCHEDULE_CALL(zx::msec(45), &CreateSoftAPTest::SetExpectMacForInds, this, kFakeMac);
  SCHEDULE_CALL(zx::msec(50), &CreateSoftAPTest::TxDeauthReq, this, kFakeMac);
  SCHEDULE_CALL(zx::msec(60), &CreateSoftAPTest::VerifyNumOfClient, this, 1);
  env_->Run();
}

}  // namespace wlan::brcmfmac
