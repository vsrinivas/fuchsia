// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wifi/wifi-config.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"
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
  void TxAssocReq();
  void TxDisassocReq();
  void ScheduleStartSoftAP(zx::duration when);
  void ScheduleVerifyStartAPConf(zx::duration when, uint8_t status);
  void ScheduleStopSoftAP(zx::duration when);
  void ScheduleAssocReq(zx::duration when);
  void ScheduleAssocDisassocReq(zx::duration when);
  void ScheduleVerifyAssoc(zx::duration when);
  void ScheduleVerifyDisassoc(zx::duration when);
  void ScheduleClearAssocInd(zx::duration when);
  void CleanupInterface();
  void VerifyAssoc();
  void VerifyDisassoc();
  void VerifyStartAPConf(uint8_t status);
  void ClearAssocInd();
  void InjectStartAPError();

 protected:
  simulation::WlanTxInfo tx_info_ = {.channel = kDefaultChannel};
  bool sec_enabled_ = false;

 private:
  // SME callbacks
  static wlanif_impl_ifc_protocol_ops_t sme_ops_;
  wlanif_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};
  std::unique_ptr<SimInterface> softap_ifc_;
  bool auth_ind_recv_ = false;
  bool assoc_ind_recv_ = false;
  bool deauth_ind_recv_ = false;
  bool disassoc_ind_recv_ = false;
  bool start_conf_received_ = false;
  uint8_t start_conf_status_;
  void OnAuthInd(const wlanif_auth_ind_t* ind);
  void OnDeauthInd(const wlanif_deauth_indication_t* ind);
  void OnAssocInd(const wlanif_assoc_ind_t* ind);
  void OnDisassocInd(const wlanif_disassoc_indication_t* ind);
  void OnStartConf(const wlanif_start_confirm_t* resp);
  void OnChannelSwitch(const wlanif_channel_switch_info_t* info);
  uint16_t CreateRsneIe(uint8_t* buffer);
};

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
          ASSERT_EQ(resp->result_code, WLAN_STOP_RESULT_SUCCESS);
        },
    .on_channel_switch =
        [](void* cookie, const wlanif_channel_switch_info_t* info) {
          static_cast<CreateSoftAPTest*>(cookie)->OnChannelSwitch(info);
        },
};

void CreateSoftAPTest::Init() { ASSERT_EQ(SimTest::Init(), ZX_OK); }

void CreateSoftAPTest::CreateInterface() {
  zx_status_t status;

  status = SimTest::CreateInterface(WLAN_INFO_MAC_ROLE_AP, sme_protocol_, &softap_ifc_);
  ASSERT_EQ(status, ZX_OK);
}

void CreateSoftAPTest::DeleteInterface() {
  uint32_t iface_id;
  zx_status_t status;

  iface_id = softap_ifc_->iface_id_;
  status = device_->WlanphyImplDestroyIface(iface_id);
  ASSERT_EQ(status, ZX_OK);
}

uint32_t CreateSoftAPTest::DeviceCount() { return (dev_mgr_->DevicesCount()); }

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
  softap_ifc_->if_impl_ops_->start_req(softap_ifc_->if_impl_ctx_, &start_req);

  // Retrieve wsec from SIM FW to check if it is set appropriately
  brcmf_simdev* sim = device_->GetSim();
  int32_t wsec;
  sim->sim_fw->IovarsGet(softap_ifc_->iface_id_, "wsec", &wsec, sizeof(wsec));
  if (sec_enabled_ == true)
    EXPECT_NE(wsec, 0);
  else
    EXPECT_EQ(wsec, 0);
  return ZX_OK;
}

void CreateSoftAPTest::InjectStartAPError() {
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjCmd(BRCMF_C_SET_SSID, ZX_ERR_IO, softap_ifc_->iface_id_);
}

zx_status_t CreateSoftAPTest::StopSoftAP() {
  wlanif_stop_req_t stop_req{
      .ssid = {.len = 6, .data = "Sim_AP"},
  };
  softap_ifc_->if_impl_ops_->stop_req(softap_ifc_->if_impl_ctx_, &stop_req);
  return ZX_OK;
}

void CreateSoftAPTest::OnAuthInd(const wlanif_auth_ind_t* ind) {
  ASSERT_EQ(std::memcmp(ind->peer_sta_address, kFakeMac.byte, ETH_ALEN), 0);
  auth_ind_recv_ = true;
}
void CreateSoftAPTest::OnDeauthInd(const wlanif_deauth_indication_t* ind) {
  ASSERT_EQ(std::memcmp(ind->peer_sta_address, kFakeMac.byte, ETH_ALEN), 0);
  deauth_ind_recv_ = true;
}
void CreateSoftAPTest::OnAssocInd(const wlanif_assoc_ind_t* ind) {
  ASSERT_EQ(std::memcmp(ind->peer_sta_address, kFakeMac.byte, ETH_ALEN), 0);
  assoc_ind_recv_ = true;
}
void CreateSoftAPTest::OnDisassocInd(const wlanif_disassoc_indication_t* ind) {
  ASSERT_EQ(std::memcmp(ind->peer_sta_address, kFakeMac.byte, ETH_ALEN), 0);
  disassoc_ind_recv_ = true;
}
void CreateSoftAPTest::OnStartConf(const wlanif_start_confirm_t* resp) {
  start_conf_received_ = true;
  start_conf_status_ = resp->result_code;
}

void CreateSoftAPTest::OnChannelSwitch(const wlanif_channel_switch_info_t* info) {}

void CreateSoftAPTest::TxAssocReq() {
  // Get the mac address of the SoftAP
  uint8_t mac_buf[ETH_ALEN];
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->IovarsGet(softap_ifc_->iface_id_, "cur_etheraddr", mac_buf, ETH_ALEN);
  common::MacAddr soft_ap_mac(mac_buf);
  const common::MacAddr mac(kFakeMac);
  simulation::SimAssocReqFrame assoc_req_frame(mac, soft_ap_mac, kDefaultSsid);
  env_->Tx(assoc_req_frame, tx_info_, this);
}

void CreateSoftAPTest::TxDisassocReq() {
  // Get the mac address of the SoftAP
  uint8_t mac_buf[ETH_ALEN];
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->IovarsGet(softap_ifc_->iface_id_, "cur_etheraddr", mac_buf, ETH_ALEN);
  common::MacAddr soft_ap_mac(mac_buf);
  const common::MacAddr mac(kFakeMac);
  // Associate with the SoftAP
  simulation::SimAssocReqFrame assoc_req_frame(mac, soft_ap_mac, kDefaultSsid);
  env_->Tx(assoc_req_frame, tx_info_, this);
  // Disassociate with the SoftAP
  simulation::SimDisassocReqFrame disassoc_req_frame(mac, soft_ap_mac, 0);
  env_->Tx(disassoc_req_frame, tx_info_, this);
}

void CreateSoftAPTest::ScheduleStartSoftAP(zx::duration when) {
  auto start_softap_fn = std::make_unique<std::function<void()>>();
  *start_softap_fn = std::bind(&CreateSoftAPTest::StartSoftAP, this);
  env_->ScheduleNotification(std::move(start_softap_fn), when);
}

void CreateSoftAPTest::ScheduleVerifyStartAPConf(zx::duration when, uint8_t status) {
  auto fn = std::make_unique<std::function<void()>>();
  *fn = std::bind(&CreateSoftAPTest::VerifyStartAPConf, this, status);
  env_->ScheduleNotification(std::move(fn), when);
}

void CreateSoftAPTest::ScheduleStopSoftAP(zx::duration when) {
  auto stop_softap_fn = std::make_unique<std::function<void()>>();
  *stop_softap_fn = std::bind(&CreateSoftAPTest::StopSoftAP, this);
  env_->ScheduleNotification(std::move(stop_softap_fn), when);
}

void CreateSoftAPTest::ScheduleAssocReq(zx::duration when) {
  auto start_assoc_fn = std::make_unique<std::function<void()>>();
  *start_assoc_fn = std::bind(&CreateSoftAPTest::TxAssocReq, this);
  env_->ScheduleNotification(std::move(start_assoc_fn), when);
}

void CreateSoftAPTest::ScheduleAssocDisassocReq(zx::duration when) {
  auto start_disassoc_fn = std::make_unique<std::function<void()>>();
  *start_disassoc_fn = std::bind(&CreateSoftAPTest::TxDisassocReq, this);
  env_->ScheduleNotification(std::move(start_disassoc_fn), when);
}

void CreateSoftAPTest::ScheduleVerifyAssoc(zx::duration when) {
  auto verify_assoc_fn = std::make_unique<std::function<void()>>();
  *verify_assoc_fn = std::bind(&CreateSoftAPTest::VerifyAssoc, this);
  env_->ScheduleNotification(std::move(verify_assoc_fn), when);
}

void CreateSoftAPTest::ScheduleVerifyDisassoc(zx::duration when) {
  auto verify_disassoc_fn = std::make_unique<std::function<void()>>();
  *verify_disassoc_fn = std::bind(&CreateSoftAPTest::VerifyDisassoc, this);
  env_->ScheduleNotification(std::move(verify_disassoc_fn), when);
}

void CreateSoftAPTest::VerifyAssoc() {
  // Verify the event indications were received and
  // the number of clients
  ASSERT_EQ(assoc_ind_recv_, true);
  ASSERT_EQ(auth_ind_recv_, true);
  brcmf_simdev* sim = device_->GetSim();
  uint16_t num_clients = sim->sim_fw->GetNumClients(softap_ifc_->iface_id_);
  ASSERT_EQ(num_clients, 1U);
}

void CreateSoftAPTest::ClearAssocInd() { assoc_ind_recv_ = false; }

void CreateSoftAPTest::ScheduleClearAssocInd(zx::duration when) {
  auto cleanup_fn = std::make_unique<std::function<void()>>();
  *cleanup_fn = std::bind(&CreateSoftAPTest::ClearAssocInd, this);
  env_->ScheduleNotification(std::move(cleanup_fn), when);
}

void CreateSoftAPTest::VerifyDisassoc() {
  // Verify the event indications were received and
  // the number of clients
  ASSERT_EQ(assoc_ind_recv_, true);
  ASSERT_EQ(auth_ind_recv_, true);
  ASSERT_EQ(disassoc_ind_recv_, true);
  ASSERT_EQ(deauth_ind_recv_, true);
  brcmf_simdev* sim = device_->GetSim();
  uint16_t num_clients = sim->sim_fw->GetNumClients(softap_ifc_->iface_id_);
  ASSERT_EQ(num_clients, 0);
}

void CreateSoftAPTest::VerifyStartAPConf(uint8_t status) {
  ASSERT_EQ(start_conf_received_, true);
  ASSERT_EQ(start_conf_status_, status);
}

void CreateSoftAPTest::CleanupInterface() { DeleteInterface(); }

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
  ScheduleStartSoftAP(delay);
  delay += kStartAPConfDelay + zx::msec(10);
  ScheduleStopSoftAP(delay);
  // Wait until SoftAP start conf is received
  // ScheduleVerifyStartAPConf(delay, WLAN_START_RESULT_SUCCESS);
  env_->Run();
  VerifyStartAPConf(WLAN_START_RESULT_SUCCESS);
  CleanupInterface();
}

TEST_F(CreateSoftAPTest, CreateSoftAPFail) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  InjectStartAPError();
  ScheduleStartSoftAP(zx::msec(50));
  env_->Run();
  VerifyStartAPConf(WLAN_START_RESULT_NOT_SUPPORTED);
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
  DeleteInterface();
}

TEST_F(CreateSoftAPTest, AssociateWithSoftAP) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  StartSoftAP();
  ScheduleAssocReq(zx::msec(10));
  ScheduleVerifyAssoc(zx::msec(50));
  env_->Run();
  CleanupInterface();
}

TEST_F(CreateSoftAPTest, ReassociateWithSoftAP) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  StartSoftAP();
  ScheduleAssocReq(zx::msec(10));
  ScheduleVerifyAssoc(zx::msec(50));
  ScheduleClearAssocInd(zx::msec(75));
  // Reassoc
  ScheduleAssocReq(zx::msec(100));
  ScheduleVerifyAssoc(zx::msec(150));
  env_->Run();
  CleanupInterface();
}

TEST_F(CreateSoftAPTest, DisassociateFromSoftAP) {
  Init();
  CreateInterface();
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  StartSoftAP();
  ScheduleAssocDisassocReq(zx::msec(50));
  ScheduleVerifyDisassoc(zx::msec(75));
  env_->Run();
  CleanupInterface();
}

}  // namespace wlan::brcmfmac
