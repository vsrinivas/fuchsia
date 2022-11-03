// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/wlan/fullmac/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <zircon/errors.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_wifi.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/macaddr.h"

namespace wlan::brcmfmac {

namespace wlan_ieee80211 = ::fuchsia::wlan::ieee80211;

constexpr wlan_channel_t kDefaultChannel = {
    .primary = 9, .cbw = CHANNEL_BANDWIDTH_CBW20, .secondary80 = 0};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
const common::MacAddr kWrongBssid({0x11, 0x22, 0x33, 0x44, 0x55, 0x66});
constexpr cssid_t kDefaultSsid = {.len = 15, .data = "Fuchsia Fake AP"};
constexpr uint8_t kIes[] = {
    // SSID
    0x00, 0x0f, 'F', 'u', 'c', 'h', 's', 'i', 'a', ' ', 'F', 'a', 'k', 'e', ' ', 'A', 'P',
    // Supported rates
    0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,
    // DS parameter set - channel 157
    0x03, 0x01, 0x9d,
    // DTIM
    0x05, 0x04, 0x00, 0x01, 0x00, 0x00,
    // Power constraint
    0x20, 0x01, 0x03,
    // HT capabilities
    0x2d, 0x1a, 0xef, 0x09, 0x1b, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // HT operation
    0x3d, 0x16, 0x9d, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Overlapping BSS scan parameters
    0x4a, 0x0e, 0x14, 0x00, 0x0a, 0x00, 0x2c, 0x01, 0xc8, 0x00, 0x14, 0x00, 0x05, 0x00, 0x19, 0x00,
    // Extended capabilities
    0x7f, 0x08, 0x01, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x40,
    // VHT capabilities
    0xbf, 0x0c, 0xb2, 0x01, 0x80, 0x33, 0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00,
    // VHT operation
    0xc0, 0x05, 0x01, 0x9b, 0x00, 0xfc, 0xff,
    // VHT Tx power envelope
    0xc3, 0x04, 0x02, 0xc4, 0xc4, 0xc4,
    // Vendor IE - WMM parameters
    0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0x80, 0x00, 0x03, 0xa4, 0x00, 0x00, 0x27, 0xa4,
    0x00, 0x00, 0x42, 0x43, 0x5e, 0x00, 0x62, 0x32, 0x2f, 0x00,
    // Vendor IE - Atheros advanced capability
    0xdd, 0x09, 0x00, 0x03, 0x7f, 0x01, 0x01, 0x00, 0x00, 0xff, 0x7f,
    // RSN
    0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
    0x00, 0x0f, 0xac, 0x02, 0x00, 0x00,
    // Vendor IE - WPS
    0xdd, 0x1d, 0x00, 0x50, 0xf2, 0x04, 0x10, 0x4a, 0x00, 0x01, 0x10, 0x10, 0x44, 0x00, 0x01, 0x02,
    0x10, 0x3c, 0x00, 0x01, 0x03, 0x10, 0x49, 0x00, 0x06, 0x00, 0x37, 0x2a, 0x00, 0x01, 0x20};
const uint8_t test_key13[WLAN_MAX_KEY_LEN] = "testingwepkey";
const uint8_t test_key5[WLAN_MAX_KEY_LEN] = "wep40";
const uint32_t kDefaultKeyIndex = 0;
const size_t kWEP40KeyLen = 5;
const size_t kWEP104KeyLen = 13;
const size_t kCommitSaeFieldsLen = 6;
// Random bits represents fake sae_fields in SAE commit authentication frame.
const uint8_t kCommitSaeFields[] = {0xAA, 0xBC, 0xFD, 0x30, 0x68, 0x21};
const size_t kConfirmSaeFieldsLen = 7;
// Random bits represents fake sae_fields in SAE confirm authentication frame.
const uint8_t kConfirmSaeFields[] = {0xBC, 0xAA, 0x33, 0x65, 0x00, 0x88, 0x94};

constexpr zx::duration kTestDuration = zx::sec(50);

class AuthTest : public SimTest {
 public:
  AuthTest() : ap_(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel) {}
  // This enum is to trigger different workflow
  enum SecurityType {
    SEC_TYPE_WEP_SHARED40,
    SEC_TYPE_WEP_SHARED104,
    SEC_TYPE_WEP_OPEN,
    SEC_TYPE_WPA1,
    SEC_TYPE_WPA2,
    SEC_TYPE_WPA3
  };

  enum SaeAuthState { COMMIT, CONFIRM, DONE };

  struct AuthFrameContent {
    AuthFrameContent(uint16_t seq_num, simulation::SimAuthType type,
                     wlan_ieee80211::StatusCode status)
        : seq_num_(seq_num), type_(type), status_(status) {}
    uint16_t seq_num_;
    simulation::SimAuthType type_;
    wlan_ieee80211::StatusCode status_;
  };

  void Init();

  // Start the process of authentication
  void StartConnect();

  void VerifyAuthFrames();
  void SecErrorInject();

  // This is the interface we will use for our single client interface
  SimInterface client_ifc_;

  SimFirmware* sim_fw_;
  uint32_t wsec_;
  uint16_t auth_;
  uint32_t wpa_auth_;
  struct brcmf_wsec_key_le wsec_key_;
  SecurityType sec_type_;
  SaeAuthState sae_auth_state_ = COMMIT;
  wlan_fullmac_sae_frame_t* sae_commit_frame = nullptr;
  bool sae_ignore_confirm = false;

  size_t auth_frame_count_ = 0;

  simulation::FakeAp ap_;

  status_code_t connect_status_ = STATUS_CODE_SUCCESS;
  std::list<AuthFrameContent> rx_auth_frames_;
  std::list<AuthFrameContent> expect_auth_frames_;
  uint8_t security_ie_[fuchsia::wlan::ieee80211::WLAN_IE_MAX_LEN];

 private:
  // Stationifc overrides
  void Rx(std::shared_ptr<const simulation::SimFrame> frame,
          std::shared_ptr<const simulation::WlanRxInfo> info) override;

  // SME callbacks
  static wlan_fullmac_impl_ifc_protocol_ops_t sme_ops_;
  wlan_fullmac_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};

  static set_key_descriptor CreateSetKeyDescriptor(const uint8_t key[WLAN_MAX_KEY_LEN],
                                                   const size_t key_count,
                                                   const uint32_t cipher_suite);
  // Event handlers
  void OnScanResult(const wlan_fullmac_scan_result_t* result);
  void OnConnectConf(const wlan_fullmac_connect_confirm_t* resp);
  void OnSaeHandshakeInd(const wlan_fullmac_sae_handshake_ind_t* ind);
  void OnSaeFrameRx(const wlan_fullmac_sae_frame_t* frame);
};

void AuthTest::Rx(std::shared_ptr<const simulation::SimFrame> frame,
                  std::shared_ptr<const simulation::WlanRxInfo> info) {
  ASSERT_EQ(frame->FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);
  auto mgmt_frame = std::static_pointer_cast<const simulation::SimManagementFrame>(frame);

  if (mgmt_frame->MgmtFrameType() != simulation::SimManagementFrame::FRAME_TYPE_AUTH) {
    return;
  }
  auto auth_frame = std::static_pointer_cast<const simulation::SimAuthFrame>(mgmt_frame);
  auth_frame_count_++;
  rx_auth_frames_.emplace_back(auth_frame->seq_num_, auth_frame->auth_type_, auth_frame->status_);
}

wlan_fullmac_impl_ifc_protocol_ops_t AuthTest::sme_ops_ = {
    .on_scan_result =
        [](void* cookie, const wlan_fullmac_scan_result_t* result) {
          static_cast<AuthTest*>(cookie)->OnScanResult(result);
        },
    .on_scan_end =
        [](void* cookie, const wlan_fullmac_scan_end_t* end) {
          // Ignore
        },
    .connect_conf =
        [](void* cookie, const wlan_fullmac_connect_confirm_t* resp) {
          static_cast<AuthTest*>(cookie)->OnConnectConf(resp);
        },
    .deauth_ind =
        [](void* cookie, const wlan_fullmac_deauth_indication_t* ind) {
          // Ignore
        },
    .disassoc_conf =
        [](void* cookie, const wlan_fullmac_disassoc_confirm_t* ind) {
          // Ignore
        },
    .signal_report = [](void* cookie, const wlan_fullmac_signal_report_indication* ind) {},
    .sae_handshake_ind =
        [](void* cookie, const wlan_fullmac_sae_handshake_ind_t* ind) {
          static_cast<AuthTest*>(cookie)->OnSaeHandshakeInd(ind);
        },
    .sae_frame_rx =
        [](void* cookie, const wlan_fullmac_sae_frame_t* frame) {
          static_cast<AuthTest*>(cookie)->OnSaeFrameRx(frame);
        },
};

void AuthTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &client_ifc_, &sme_protocol_), ZX_OK);
  sim_fw_ = device_->GetSim()->sim_fw.get();
  ap_.EnableBeacon(zx::msec(100));
}

void AuthTest::VerifyAuthFrames() {
  EXPECT_EQ(rx_auth_frames_.size(), expect_auth_frames_.size());

  while (!expect_auth_frames_.empty()) {
    ASSERT_EQ(rx_auth_frames_.empty(), false);
    AuthFrameContent rx_content = rx_auth_frames_.front();
    AuthFrameContent expect_content = expect_auth_frames_.front();
    EXPECT_EQ(rx_content.seq_num_, expect_content.seq_num_);
    EXPECT_EQ(rx_content.type_, expect_content.type_);
    EXPECT_EQ(rx_content.status_, expect_content.status_);
    rx_auth_frames_.pop_front();
    expect_auth_frames_.pop_front();
  }
}

void AuthTest::SecErrorInject() {
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjIovar("wsec", ZX_ERR_IO, BCME_OK, client_ifc_.iface_id_);
}

set_key_descriptor AuthTest::CreateSetKeyDescriptor(const uint8_t key[WLAN_MAX_KEY_LEN],
                                                    const size_t key_count,
                                                    const uint32_t cipher_suite) {
  set_key_descriptor key_des = {.key_list = key};
  key_des.key_count = key_count;
  key_des.key_id = kDefaultKeyIndex;
  memcpy(key_des.address, kDefaultBssid.byte, ETH_ALEN);
  key_des.cipher_suite_type = cipher_suite;
  return key_des;
}

void AuthTest::StartConnect() {
  wlan_fullmac_connect_req connect_req = {};
  memcpy(connect_req.selected_bss.bssid, kDefaultBssid.byte, ETH_ALEN);
  connect_req.selected_bss.ies_list = kIes;
  connect_req.selected_bss.ies_count = sizeof(kIes);
  connect_req.selected_bss.channel = kDefaultChannel;
  connect_req.connect_failure_timeout = 1000;

  // Fill out the auth_type arg
  switch (sec_type_) {
    case SEC_TYPE_WEP_SHARED104: {
      connect_req.wep_key =
          CreateSetKeyDescriptor(&test_key13[0], kWEP104KeyLen, WPA_CIPHER_WEP_104);
      connect_req.auth_type = WLAN_AUTH_TYPE_SHARED_KEY;
      break;
    }

    case SEC_TYPE_WEP_SHARED40: {
      connect_req.wep_key = CreateSetKeyDescriptor(&test_key5[0], kWEP40KeyLen, WPA_CIPHER_WEP_40);
      connect_req.auth_type = WLAN_AUTH_TYPE_SHARED_KEY;
      break;
    }

    case SEC_TYPE_WEP_OPEN: {
      connect_req.wep_key = CreateSetKeyDescriptor(&test_key5[0], kWEP40KeyLen, WPA_CIPHER_WEP_40);
      connect_req.auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM;
      break;
    }

    case SEC_TYPE_WPA1:
    case SEC_TYPE_WPA2: {
      connect_req.auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM;
      break;
    }
    case SEC_TYPE_WPA3: {
      connect_req.auth_type = WLAN_AUTH_TYPE_SAE;
      break;
    }

    default:
      return;
  }

  // Fill out the security_ie arg
  connect_req.security_ie_list = security_ie_;
  if (sec_type_ == SEC_TYPE_WEP_SHARED104 || sec_type_ == SEC_TYPE_WEP_SHARED40 ||
      sec_type_ == SEC_TYPE_WEP_OPEN) {
    connect_req.security_ie_count = 0;
  } else if (sec_type_ == SEC_TYPE_WPA1) {
    // construct a fake vendor ie in wlan_fullmac_assoc_req.
    uint16_t offset = 0;
    security_ie_[offset++] = WLAN_IE_TYPE_VENDOR_SPECIFIC;
    security_ie_[offset++] = 22;  // The length of following content.

    memcpy(&security_ie_[offset], MSFT_OUI, TLV_OUI_LEN);
    offset += TLV_OUI_LEN;
    security_ie_[offset++] = WPA_OUI_TYPE;

    // These two bytes are 16-bit version number.
    security_ie_[offset++] = 1;  // Lower byte
    security_ie_[offset++] = 0;  // Higher byte

    memcpy(&security_ie_[offset], MSFT_OUI, TLV_OUI_LEN);
    offset += TLV_OUI_LEN;
    security_ie_[offset++] = WPA_CIPHER_TKIP;  // Set multicast cipher suite.

    // These two bytes indicate the length of unicast cipher list, in this case is 1.
    security_ie_[offset++] = 1;  // Lower byte
    security_ie_[offset++] = 0;  // Higher byte

    memcpy(&security_ie_[offset], MSFT_OUI, TLV_OUI_LEN);
    offset += TLV_OUI_LEN;                         // The second WPA OUI.
    security_ie_[offset++] = WPA_CIPHER_CCMP_128;  // Set unicast cipher suite.

    // These two bytes indicate the length of auth management suite list, in this case is 1.
    security_ie_[offset++] = 1;  // Lower byte
    security_ie_[offset++] = 0;  // Higher byte

    memcpy(&security_ie_[offset], MSFT_OUI, TLV_OUI_LEN);  // WPA OUI for auth management suite.
    offset += TLV_OUI_LEN;
    security_ie_[offset++] = RSN_AKM_PSK;  // Set auth management suite.

    connect_req.security_ie_count = offset;
    ASSERT_EQ(connect_req.security_ie_count,
              (const uint32_t)(security_ie_[TLV_LEN_OFF] + TLV_HDR_LEN));
  } else if (sec_type_ == SEC_TYPE_WPA2) {
    // construct a fake rsne ie in wlan_fullmac_assoc_req.
    uint16_t offset = 0;
    security_ie_[offset++] = WLAN_IE_TYPE_RSNE;
    security_ie_[offset++] = 20;  // The length of following content.

    // These two bytes are 16-bit version number.
    security_ie_[offset++] = 1;  // Lower byte
    security_ie_[offset++] = 0;  // Higher byte

    memcpy(&security_ie_[offset], RSN_OUI, TLV_OUI_LEN);  // RSN OUI for multicast cipher suite.
    offset += TLV_OUI_LEN;
    security_ie_[offset++] = WPA_CIPHER_TKIP;  // Set multicast cipher suite.

    // These two bytes indicate the length of unicast cipher list, in this case is 1.
    security_ie_[offset++] = 1;  // Lower byte
    security_ie_[offset++] = 0;  // Higher byte

    memcpy(&security_ie_[offset], RSN_OUI, TLV_OUI_LEN);  // RSN OUI for unicast cipher suite.
    offset += TLV_OUI_LEN;
    security_ie_[offset++] = WPA_CIPHER_CCMP_128;  // Set unicast cipher suite.

    // These two bytes indicate the length of auth management suite list, in this case is 1.
    security_ie_[offset++] = 1;  // Lower byte
    security_ie_[offset++] = 0;  // Higher byte

    memcpy(&security_ie_[offset], RSN_OUI, TLV_OUI_LEN);  // RSN OUI for auth management suite.
    offset += TLV_OUI_LEN;
    security_ie_[offset++] = RSN_AKM_PSK;  // Set auth management suite.

    // These two bytes indicate RSN capabilities, in this case is \x0c\x00.
    security_ie_[offset++] = 12;  // Lower byte
    security_ie_[offset++] = 0;   // Higher byte

    connect_req.security_ie_count = offset;
    ASSERT_EQ(connect_req.security_ie_count,
              (const uint32_t)(security_ie_[TLV_LEN_OFF] + TLV_HDR_LEN));
  } else if (sec_type_ == SEC_TYPE_WPA3) {
    uint16_t offset = 0;
    security_ie_[offset++] = WLAN_IE_TYPE_RSNE;
    security_ie_[offset++] = 20;  // The length of following content.

    // These two bytes are 16-bit version number.
    security_ie_[offset++] = 1;  // Lower byte
    security_ie_[offset++] = 0;  // Higher byte

    memcpy(&security_ie_[offset], RSN_OUI, TLV_OUI_LEN);  // RSN OUI for multicast cipher suite.
    offset += TLV_OUI_LEN;
    security_ie_[offset++] = WPA_CIPHER_CCMP_128;  // Set multicast cipher suite.

    // These two bytes indicate the length of unicast cipher list, in this case is 1.
    security_ie_[offset++] = 1;  // Lower byte
    security_ie_[offset++] = 0;  // Higher byte

    memcpy(&security_ie_[offset], RSN_OUI, TLV_OUI_LEN);  // RSN OUI for unicast cipher suite.
    offset += TLV_OUI_LEN;
    security_ie_[offset++] = WPA_CIPHER_CCMP_128;  // Set unicast cipher suite.

    // These two bytes indicate the length of auth management suite list, in this case is 1.
    security_ie_[offset++] = 1;  // Lower byte
    security_ie_[offset++] = 0;  // Higher byte

    memcpy(&security_ie_[offset], RSN_OUI, TLV_OUI_LEN);  // RSN OUI for auth management suite.
    offset += TLV_OUI_LEN;
    security_ie_[offset++] = RSN_AKM_SAE_PSK;  // Set auth management suite.

    // These two bytes indicate RSN capabilities, in this case is \x0c\x00.
    security_ie_[offset++] = 12;  // Lower byte
    security_ie_[offset++] = 0;   // Higher byte

    connect_req.security_ie_count = offset;
    ASSERT_EQ(connect_req.security_ie_count,
              (const uint32_t)(security_ie_[TLV_LEN_OFF] + TLV_HDR_LEN));
  }

  client_ifc_.if_impl_ops_->connect_req(client_ifc_.if_impl_ctx_, &connect_req);
}

void AuthTest::OnScanResult(const wlan_fullmac_scan_result_t* result) {
  EXPECT_EQ(result->bss.capability_info, (uint16_t)32);
}

void AuthTest::OnConnectConf(const wlan_fullmac_connect_confirm_t* resp) {
  connect_status_ = resp->result_code;
  if (connect_status_ != STATUS_CODE_SUCCESS) {
    return;
  }

  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, client_ifc_.iface_id_);
  zx_status_t status;

  {
    // Since auth_ is uint16_t, we need a uint32_t to get the iovar
    uint32_t auth32;
    status = brcmf_fil_bsscfg_int_get(ifp, "auth", &auth32);
    auth_ = static_cast<uint16_t>(auth32);
    EXPECT_EQ(status, ZX_OK);
  }

  status = brcmf_fil_bsscfg_int_get(ifp, "wsec", &wsec_);
  EXPECT_EQ(status, ZX_OK);

  status = brcmf_fil_bsscfg_int_get(ifp, "wpa_auth", &wpa_auth_);
  EXPECT_EQ(status, ZX_OK);

  // The wsec_key iovar is only meaningful for WEP security
  if (sec_type_ == SEC_TYPE_WEP_SHARED104 || sec_type_ == SEC_TYPE_WEP_SHARED40 ||
      sec_type_ == SEC_TYPE_WEP_OPEN) {
    status = brcmf_fil_bsscfg_data_get(ifp, "wsec_key", &wsec_key_, sizeof(wsec_key_));
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(wsec_key_.flags, (uint32_t)BRCMF_PRIMARY_KEY);
    EXPECT_EQ(wsec_key_.index, kDefaultKeyIndex);
  }

  switch (sec_type_) {
    case SEC_TYPE_WEP_SHARED104:
      EXPECT_EQ(wsec_, (uint32_t)WEP_ENABLED);
      // The driver would have set BRCMF_AUTH_MODE_AUTO, but the sim_fw changes auth_ to
      // BRCMF_AUTH_MODE_OPEN when it sees the fake AP rejecting the first auth request.
      EXPECT_EQ(auth_, (uint16_t)BRCMF_AUTH_MODE_OPEN);
      EXPECT_EQ(wsec_key_.algo, (uint32_t)CRYPTO_ALGO_WEP128);
      EXPECT_EQ(wsec_key_.len, kWEP104KeyLen);
      EXPECT_EQ(memcmp(test_key13, wsec_key_.data, kWEP104KeyLen), 0);
      break;
    case SEC_TYPE_WEP_SHARED40:
      EXPECT_EQ(wsec_, (uint32_t)WEP_ENABLED);
      EXPECT_EQ(auth_, (uint16_t)BRCMF_AUTH_MODE_AUTO);
      EXPECT_EQ(wsec_key_.algo, (uint32_t)CRYPTO_ALGO_WEP1);
      EXPECT_EQ(wsec_key_.len, kWEP40KeyLen);
      EXPECT_EQ(memcmp(test_key5, wsec_key_.data, kWEP40KeyLen), 0);
      break;
    case SEC_TYPE_WEP_OPEN:
      EXPECT_EQ(wsec_, (uint32_t)WEP_ENABLED);
      EXPECT_EQ(auth_, (uint16_t)BRCMF_AUTH_MODE_OPEN);
      EXPECT_EQ(wsec_key_.algo, (uint32_t)CRYPTO_ALGO_WEP1);
      EXPECT_EQ(wsec_key_.len, kWEP40KeyLen);
      EXPECT_EQ(memcmp(test_key5, wsec_key_.data, kWEP40KeyLen), 0);
      break;
    case SEC_TYPE_WPA1:
      EXPECT_EQ(wsec_, (uint32_t)(TKIP_ENABLED | AES_ENABLED));
      EXPECT_EQ(wpa_auth_, (uint32_t)WPA_AUTH_PSK);
      break;
    case SEC_TYPE_WPA2:
      EXPECT_EQ(wsec_, (uint32_t)(TKIP_ENABLED | AES_ENABLED));
      EXPECT_EQ(wpa_auth_, (uint32_t)WPA2_AUTH_PSK);
      break;
    case SEC_TYPE_WPA3:
      EXPECT_EQ(wsec_, (uint32_t)(AES_ENABLED));
      EXPECT_EQ(wpa_auth_, (uint32_t)WPA3_AUTH_SAE_PSK);
      break;
    default:
      break;
  }
}

void AuthTest::OnSaeHandshakeInd(const wlan_fullmac_sae_handshake_ind_t* ind) {
  common::MacAddr peer_sta_addr(ind->peer_sta_address);
  ASSERT_EQ(peer_sta_addr, kDefaultBssid);

  // Send the error injected commit frame instead if it exists.
  if (sae_commit_frame != nullptr) {
    client_ifc_.if_impl_ops_->sae_frame_tx(client_ifc_.if_impl_ctx_, sae_commit_frame);
    return;
  }

  wlan_fullmac_sae_frame_t frame = {
      .status_code = static_cast<uint16_t>(wlan_ieee80211::StatusCode::SUCCESS),
      .seq_num = 1,
      .sae_fields_list = kCommitSaeFields,
      .sae_fields_count = kCommitSaeFieldsLen};

  kDefaultBssid.CopyTo(frame.peer_sta_address);

  client_ifc_.if_impl_ops_->sae_frame_tx(client_ifc_.if_impl_ctx_, &frame);
}

void AuthTest::OnSaeFrameRx(const wlan_fullmac_sae_frame_t* frame) {
  common::MacAddr peer_sta_addr(frame->peer_sta_address);
  ASSERT_EQ(peer_sta_addr, kDefaultBssid);

  if (sae_auth_state_ == COMMIT) {
    ASSERT_EQ(frame->seq_num, 1);
    EXPECT_EQ(frame->status_code, static_cast<uint16_t>(wlan_ieee80211::StatusCode::SUCCESS));
    EXPECT_EQ(frame->sae_fields_count, kCommitSaeFieldsLen);
    EXPECT_EQ(memcmp(frame->sae_fields_list, kCommitSaeFields, kCommitSaeFieldsLen), 0);
    wlan_fullmac_sae_frame_t next_frame = {
        .status_code = static_cast<uint16_t>(wlan_ieee80211::StatusCode::SUCCESS),
        .seq_num = 2,
        .sae_fields_list = kConfirmSaeFields,
        .sae_fields_count = kConfirmSaeFieldsLen};

    kDefaultBssid.CopyTo(next_frame.peer_sta_address);

    client_ifc_.if_impl_ops_->sae_frame_tx(client_ifc_.if_impl_ctx_, &next_frame);
    sae_auth_state_ = CONFIRM;
  } else if (sae_auth_state_ == CONFIRM) {
    ASSERT_EQ(frame->seq_num, 2);
    EXPECT_EQ(frame->status_code, static_cast<uint16_t>(wlan_ieee80211::StatusCode::SUCCESS));
    EXPECT_EQ(frame->sae_fields_count, kConfirmSaeFieldsLen);
    EXPECT_EQ(memcmp(frame->sae_fields_list, kConfirmSaeFields, kConfirmSaeFieldsLen), 0);

    if (sae_ignore_confirm)
      return;

    wlan_fullmac_sae_handshake_resp_t resp = {
        .status_code = static_cast<uint16_t>(wlan_ieee80211::StatusCode::SUCCESS)};
    kDefaultBssid.CopyTo(resp.peer_sta_address);
    client_ifc_.if_impl_ops_->sae_handshake_resp(client_ifc_.if_impl_ctx_, &resp);
    sae_auth_state_ = DONE;
  }
}

/*In the test part, we are actually testing two stages independently in each test case, for example
 * in WEP104. The first stage is to test whether the iovars are correctly set into sim-fw, and
 * whether the 13-byte key is consistent. The second stage is testing whether sim-fw is doing the
 * right thing when we use SHARED KEY mode to do authencation with an AP in OPEN SYSTEM mode. If we
 * change the first stage to WEP40, the second stage should have the same behaviour.
 */
TEST_F(AuthTest, WEP104) {
  Init();
  sec_type_ = SEC_TYPE_WEP_SHARED104;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_WEP});
  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);
  // The first SHARED KEY authentication request will fail, and switch to OPEN SYSTEM automatically.
  // OPEN SYSTEM authentication will succeed.
  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_SHARED_KEY,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_SHARED_KEY,
                                   wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED);
  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_OPEN,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_OPEN,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  VerifyAuthFrames();
}

TEST_F(AuthTest, WEP40) {
  Init();
  sec_type_ = SEC_TYPE_WEP_SHARED40;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_SHARED_KEY,
                   .sec_type = simulation::SEC_PROTO_TYPE_WEP});
  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);
  // It should be a successful shared_key authentication
  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_SHARED_KEY,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_SHARED_KEY,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(3, simulation::AUTH_TYPE_SHARED_KEY,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(4, simulation::AUTH_TYPE_SHARED_KEY,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  VerifyAuthFrames();
}

TEST_F(AuthTest, WEP40ChallengeFailure) {
  Init();
  sec_type_ = SEC_TYPE_WEP_SHARED40;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_SHARED_KEY,
                   .sec_type = simulation::SEC_PROTO_TYPE_WEP,
                   .expect_challenge_failure = true});
  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);
  // It should be a failed shared_key authentication
  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_SHARED_KEY,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_SHARED_KEY,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(3, simulation::AUTH_TYPE_SHARED_KEY,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(4, simulation::AUTH_TYPE_SHARED_KEY,
                                   wlan_ieee80211::StatusCode::CHALLENGE_FAILURE);
  VerifyAuthFrames();

  // Assoc should have failed
  EXPECT_NE(connect_status_, STATUS_CODE_SUCCESS);
}

TEST_F(AuthTest, WEPOPEN) {
  Init();
  sec_type_ = SEC_TYPE_WEP_OPEN;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_WEP});
  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_OPEN,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_OPEN,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  VerifyAuthFrames();
}

TEST_F(AuthTest, AuthFailTest) {
  Init();
  sec_type_ = SEC_TYPE_WEP_OPEN;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_OPEN});
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjIovar("auth", ZX_ERR_IO, BCME_OK, client_ifc_.iface_id_);
  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);
  EXPECT_NE(connect_status_, STATUS_CODE_SUCCESS);
}

TEST_F(AuthTest, WEPIgnoreTest) {
  Init();
  sec_type_ = SEC_TYPE_WEP_OPEN;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_WEP});
  ap_.SetAssocHandling(simulation::FakeAp::ASSOC_IGNORED);

  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));
  env_->Run(kTestDuration);

  // The auth request frames should be ignored and the auth timeout in sim-fw will be triggered,
  // AuthHandleFailure() will retry for "max_retries" times and will send an BRCMF_E_SET_SSID event
  // with status BRCMF_E_STATUS_FAIL finally.
  uint32_t max_retries = 0;
  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, client_ifc_.iface_id_);
  zx_status_t status = brcmf_fil_iovar_int_get(ifp, "assoc_retry_max", &max_retries, nullptr);
  EXPECT_EQ(status, ZX_OK);
  for (uint32_t i = 0; i < max_retries + 1; i++) {
    expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_OPEN,
                                     wlan_ieee80211::StatusCode::SUCCESS);
  }
  VerifyAuthFrames();
  EXPECT_EQ(connect_status_, STATUS_CODE_REFUSED_REASON_UNSPECIFIED);
}

TEST_F(AuthTest, WPA1Test) {
  Init();
  sec_type_ = SEC_TYPE_WPA1;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_WPA1});
  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_OPEN,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_OPEN,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  VerifyAuthFrames();
  // Make sure that OnConnectConf is called, so the check inside is called.
  EXPECT_EQ(connect_status_, STATUS_CODE_SUCCESS);
}

TEST_F(AuthTest, WPA1FailTest) {
  Init();
  sec_type_ = SEC_TYPE_WPA1;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_WPA1});
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjIovar("wpaie", ZX_ERR_IO, BCME_OK, client_ifc_.iface_id_);
  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  // Assoc should have failed
  EXPECT_NE(connect_status_, STATUS_CODE_SUCCESS);
}

TEST_F(AuthTest, WPA2Test) {
  Init();
  sec_type_ = SEC_TYPE_WPA2;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_WPA2});
  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_OPEN,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_OPEN,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  VerifyAuthFrames();
  // Make sure that OnConnectConf is called, so the check inside is called.
  EXPECT_EQ(connect_status_, STATUS_CODE_SUCCESS);
}

TEST_F(AuthTest, WPA2FailTest) {
  Init();
  sec_type_ = SEC_TYPE_WPA2;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_WPA2});
  SecErrorInject();
  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);
  // Make sure that OnConnectConf is called, so the check inside is called.
  EXPECT_NE(connect_status_, STATUS_CODE_SUCCESS);
}

// This test case verifies that auth req will be refused when security types of client and AP are
// not matched.
TEST_F(AuthTest, WrongSecTypeAuthFail) {
  Init();
  // Client sec type is WPA1 while AP's is WEP.
  sec_type_ = SEC_TYPE_WPA1;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_WEP});
  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  uint32_t max_retries = 0;

  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, client_ifc_.iface_id_);
  zx_status_t status = brcmf_fil_iovar_int_get(ifp, "assoc_retry_max", &max_retries, nullptr);
  EXPECT_EQ(status, ZX_OK);

  for (uint32_t i = 0; i < max_retries + 1; i++) {
    expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_OPEN,
                                     wlan_ieee80211::StatusCode::SUCCESS);
    expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_OPEN,
                                     wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED);
  }

  VerifyAuthFrames();
  EXPECT_EQ(connect_status_, STATUS_CODE_REFUSED_REASON_UNSPECIFIED);
}

// Verify a normal SAE authentication work flow in driver.
TEST_F(AuthTest, WPA3Test) {
  Init();
  sec_type_ = SEC_TYPE_WPA3;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_SAE,
                   .sec_type = simulation::SEC_PROTO_TYPE_WPA3});
  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));

  env_->Run(kTestDuration);

  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_SAE,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_SAE,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_SAE,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_SAE,
                                   wlan_ieee80211::StatusCode::SUCCESS);

  VerifyAuthFrames();
  // Make sure that OnConnectConf is called, so the check inside is called.
  EXPECT_EQ(connect_status_, STATUS_CODE_SUCCESS);
  EXPECT_EQ(sae_auth_state_, DONE);
}

// Verify that firmware will timeout if AP ignores the SAE auth frame.
TEST_F(AuthTest, WPA3ApIgnoreTest) {
  Init();
  sec_type_ = SEC_TYPE_WPA3;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_SAE,
                   .sec_type = simulation::SEC_PROTO_TYPE_WPA3});
  ap_.SetAssocHandling(simulation::FakeAp::ASSOC_IGNORED);
  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));
  env_->Run(kTestDuration);

  // Make sure firmware will not retry for external supplicant authentication.
  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_SAE,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  VerifyAuthFrames();
  EXPECT_EQ(connect_status_, STATUS_CODE_REJECTED_SEQUENCE_TIMEOUT);
  EXPECT_EQ(sae_auth_state_, COMMIT);
}

// Verify that firmware will timeout if external supplicant ignore the final SAE auth frame and fail
// to send out the handshake response.
TEST_F(AuthTest, WPA3SupplicantIgnoreTest) {
  Init();
  sec_type_ = SEC_TYPE_WPA3;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_SAE,
                   .sec_type = simulation::SEC_PROTO_TYPE_WPA3});
  sae_ignore_confirm = true;
  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));
  env_->Run(kTestDuration);

  // Make sure firmware will not retry for external supplicant authentication.
  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_SAE,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_SAE,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_SAE,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_SAE,
                                   wlan_ieee80211::StatusCode::SUCCESS);
  VerifyAuthFrames();

  EXPECT_EQ(connect_status_, STATUS_CODE_REJECTED_SEQUENCE_TIMEOUT);
  EXPECT_EQ(sae_auth_state_, CONFIRM);
}

// Verify that the AP will not reply to the sae frame with non-success status code, firmware will
// timeout.
TEST_F(AuthTest, WPA3FailStatusCode) {
  Init();
  sec_type_ = SEC_TYPE_WPA3;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_SAE,
                   .sec_type = simulation::SEC_PROTO_TYPE_WPA3});
  ap_.SetAssocHandling(simulation::FakeAp::ASSOC_IGNORED);

  wlan_fullmac_sae_frame_t frame = {
      .status_code = static_cast<uint16_t>(wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED),
      .seq_num = 1,
      .sae_fields_list = kCommitSaeFields,
      .sae_fields_count = kCommitSaeFieldsLen};

  kDefaultBssid.CopyTo(frame.peer_sta_address);
  sae_commit_frame = &frame;

  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));
  env_->Run(kTestDuration);

  // Make sure firmware will not retry for external supplicant authentication.
  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_SAE,
                                   wlan_ieee80211::StatusCode::REFUSED_REASON_UNSPECIFIED);
  VerifyAuthFrames();
  EXPECT_EQ(connect_status_, STATUS_CODE_REJECTED_SEQUENCE_TIMEOUT);
  EXPECT_EQ(sae_auth_state_, COMMIT);
}

// Verify that the firmware will timeout if the bssid in SAE auth frame is wrong, because SAE status
// will not be updated.
TEST_F(AuthTest, WPA3WrongBssid) {
  Init();
  sec_type_ = SEC_TYPE_WPA3;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_SAE,
                   .sec_type = simulation::SEC_PROTO_TYPE_WPA3});
  ap_.SetAssocHandling(simulation::FakeAp::ASSOC_IGNORED);

  wlan_fullmac_sae_frame_t frame = {
      .status_code = static_cast<uint16_t>(wlan_ieee80211::StatusCode::SUCCESS),
      .seq_num = 1,
      .sae_fields_list = kCommitSaeFields,
      .sae_fields_count = kCommitSaeFieldsLen};
  // Use wrong bssid.
  kWrongBssid.CopyTo(frame.peer_sta_address);
  sae_commit_frame = &frame;

  env_->ScheduleNotification(std::bind(&AuthTest::StartConnect, this), zx::msec(10));
  env_->Run(kTestDuration);

  // No auth frame will be sent out.
  VerifyAuthFrames();
  EXPECT_EQ(connect_status_, STATUS_CODE_REJECTED_SEQUENCE_TIMEOUT);
  EXPECT_EQ(sae_auth_state_, COMMIT);
}

}  // namespace wlan::brcmfmac
