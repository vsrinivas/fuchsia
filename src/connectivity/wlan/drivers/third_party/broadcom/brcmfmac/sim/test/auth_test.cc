// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/wlanif.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_wifi.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::brcmfmac {

constexpr wlan_channel_t kDefaultChannel = {
    .primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
constexpr wlan_ssid_t kDefaultSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const uint8_t test_key13[WLAN_MAX_KEY_LEN] = "testingwepkey";
const uint8_t test_key5[WLAN_MAX_KEY_LEN] = "wep40";
const uint32_t kDefaultKeyIndex = 0;
const size_t kWEP40KeyLen = 5;
const size_t kWEP104KeyLen = 13;

constexpr zx::duration kTestDuration = zx::sec(50);

class AuthTest : public SimTest {
 public:
  AuthTest() : ap_(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel){};
  // This enum is to trigger different workflow
  enum SecurityType {
    SEC_TYPE_WEP_SHARED40,
    SEC_TYPE_WEP_SHARED104,
    SEC_TYPE_WEP_OPEN,
    SEC_TYPE_WPA1,
    SEC_TYPE_WPA2
  };
  struct AuthFrameContent {
    AuthFrameContent(uint16_t seq_num, simulation::SimAuthType type, uint16_t status)
        : seq_num_(seq_num), type_(type), status_(status) {}
    uint16_t seq_num_;
    simulation::SimAuthType type_;
    uint16_t status_;
  };

  void Init();

  // Start the process of authentication
  void StartAuth();

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

  size_t auth_frame_count_ = 0;

  simulation::FakeAp ap_;

  uint8_t auth_status_ = WLAN_AUTH_RESULT_SUCCESS;
  uint8_t assoc_status_ = WLAN_ASSOC_RESULT_SUCCESS;
  std::list<AuthFrameContent> rx_auth_frames_;
  std::list<AuthFrameContent> expect_auth_frames_;

 private:
  // Stationifc overrides
  void Rx(std::shared_ptr<const simulation::SimFrame> frame,
          std::shared_ptr<const simulation::WlanRxInfo> info) override;

  // SME callbacks
  static wlanif_impl_ifc_protocol_ops_t sme_ops_;
  wlanif_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};

  wlanif_set_keys_req CreateKeyReq(const uint8_t key[WLAN_MAX_KEY_LEN], const size_t key_count,
                                   const uint32_t cipher_suite);
  // Event handlers
  void OnScanResult(const wlanif_scan_result_t* result);
  void OnJoinConf(const wlanif_join_confirm_t* resp);
  void OnAuthConf(const wlanif_auth_confirm_t* resp);
  void OnAssocConf(const wlanif_assoc_confirm_t* resp);
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

wlanif_impl_ifc_protocol_ops_t AuthTest::sme_ops_ = {
    .on_scan_result =
        [](void* cookie, const wlanif_scan_result_t* result) {
          static_cast<AuthTest*>(cookie)->OnScanResult(result);
        },
    .on_scan_end =
        [](void* cookie, const wlanif_scan_end_t* end) {
          // Ignore
        },
    .join_conf =
        [](void* cookie, const wlanif_join_confirm_t* resp) {
          static_cast<AuthTest*>(cookie)->OnJoinConf(resp);
        },
    .auth_conf =
        [](void* cookie, const wlanif_auth_confirm_t* resp) {
          static_cast<AuthTest*>(cookie)->OnAuthConf(resp);
        },
    .deauth_ind =
        [](void* cookie, const wlanif_deauth_indication_t* ind) {
          // Ignore
        },
    .assoc_conf =
        [](void* cookie, const wlanif_assoc_confirm_t* resp) {
          static_cast<AuthTest*>(cookie)->OnAssocConf(resp);
        },
    .signal_report = [](void* cookie, const wlanif_signal_report_indication* ind) {},
};

void AuthTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_, &sme_protocol_), ZX_OK);
  sim_fw_ = device_->GetSim()->sim_fw.get();
  ap_.EnableBeacon(zx::msec(100));
}

void AuthTest::VerifyAuthFrames() {
  EXPECT_EQ(rx_auth_frames_.size(), expect_auth_frames_.size());

  while (!expect_auth_frames_.empty()) {
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
  sim->sim_fw->err_inj_.AddErrInjIovar("wsec", ZX_ERR_IO, client_ifc_.iface_id_);
}

wlanif_set_keys_req AuthTest::CreateKeyReq(const uint8_t key[WLAN_MAX_KEY_LEN],
                                           const size_t key_count, const uint32_t cipher_suite) {
  set_key_descriptor key_des = {.key_list = key};
  key_des.key_count = key_count;
  key_des.key_id = kDefaultKeyIndex;
  memcpy(key_des.address, kDefaultBssid.byte, ETH_ALEN);
  key_des.cipher_suite_type = cipher_suite;

  wlanif_set_keys_req set_keys_req = {};
  set_keys_req.num_keys = 1;
  set_keys_req.keylist[0] = key_des;
  return set_keys_req;
}

void AuthTest::StartAuth() {
  wlanif_join_req join_req = {};
  memcpy(join_req.selected_bss.bssid, kDefaultBssid.byte, ETH_ALEN);
  join_req.selected_bss.ssid.len = kDefaultSsid.len;
  memcpy(join_req.selected_bss.ssid.data, kDefaultSsid.ssid, WLAN_MAX_SSID_LEN);
  join_req.selected_bss.chan = kDefaultChannel;
  client_ifc_.if_impl_ops_->join_req(client_ifc_.if_impl_ctx_, &join_req);
}

void AuthTest::OnScanResult(const wlanif_scan_result_t* result) {
  EXPECT_EQ(result->bss.cap, (uint16_t)32);
  EXPECT_EQ(result->bss.rsne_len, (size_t)0);
}

void AuthTest::OnJoinConf(const wlanif_join_confirm_t* resp) {
  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, client_ifc_.iface_id_);
  zx_status_t status;

  sim_fw_->IovarsGet(client_ifc_.iface_id_, "wsec", &wsec_, sizeof(wsec_), nullptr);
  EXPECT_EQ(wsec_, (uint32_t)0);

  status = brcmf_fil_bsscfg_int_get(ifp, "wpa_auth", &wpa_auth_);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(wpa_auth_, (uint32_t)0);

  // Prepare auth request
  wlanif_auth_req_t auth_req;
  std::memcpy(auth_req.peer_sta_address, kDefaultBssid.byte, ETH_ALEN);
  auth_req.auth_failure_timeout = 1000;

  switch (sec_type_) {
    case SEC_TYPE_WEP_SHARED104: {
      wlanif_set_keys_req set_keys_req =
          CreateKeyReq(&test_key13[0], kWEP104KeyLen, WPA_CIPHER_WEP_104);
      client_ifc_.if_impl_ops_->set_keys_req(client_ifc_.if_impl_ctx_, &set_keys_req);
      auth_req.auth_type = WLAN_AUTH_TYPE_SHARED_KEY;
      break;
    }

    case SEC_TYPE_WEP_SHARED40: {
      wlanif_set_keys_req set_keys_req =
          CreateKeyReq(&test_key5[0], kWEP40KeyLen, WPA_CIPHER_WEP_40);
      client_ifc_.if_impl_ops_->set_keys_req(client_ifc_.if_impl_ctx_, &set_keys_req);
      auth_req.auth_type = WLAN_AUTH_TYPE_SHARED_KEY;
      break;
    }

    case SEC_TYPE_WEP_OPEN: {
      wlanif_set_keys_req set_keys_req =
          CreateKeyReq(&test_key5[0], kWEP40KeyLen, WPA_CIPHER_WEP_40);
      client_ifc_.if_impl_ops_->set_keys_req(client_ifc_.if_impl_ctx_, &set_keys_req);
      auth_req.auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM;
      break;
    }

    case SEC_TYPE_WPA1:
    case SEC_TYPE_WPA2: {
      auth_req.auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM;
      break;
    }

    default:
      return;
  }
  client_ifc_.if_impl_ops_->auth_req(client_ifc_.if_impl_ctx_, &auth_req);
}

void AuthTest::OnAuthConf(const wlanif_auth_confirm_t* resp) {
  auth_status_ = resp->result_code;
  if (auth_status_ != WLAN_AUTH_RESULT_SUCCESS) {
    return;
  }
  sim_fw_->IovarsGet(client_ifc_.iface_id_, "wsec_key", &wsec_key_, sizeof(wsec_key_), nullptr);
  sim_fw_->IovarsGet(client_ifc_.iface_id_, "auth", &auth_, sizeof(auth_), nullptr);
  sim_fw_->IovarsGet(client_ifc_.iface_id_, "wsec", &wsec_, sizeof(wsec_), nullptr);

  if (sec_type_ != SEC_TYPE_WPA1 && sec_type_ != SEC_TYPE_WPA2) {
    EXPECT_EQ(wsec_key_.flags, (uint32_t)BRCMF_PRIMARY_KEY);
    EXPECT_EQ(wsec_key_.index, kDefaultKeyIndex);
  }

  switch (sec_type_) {
    case SEC_TYPE_WEP_SHARED104:
      EXPECT_EQ(wsec_, (uint32_t)WEP_ENABLED);
      EXPECT_EQ(auth_, (uint16_t)BRCMF_AUTH_MODE_AUTO);
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
    case SEC_TYPE_WPA2:
      // wsec iovar is not set before assoc_req sending to driver, now it should be default value.
      EXPECT_EQ(wsec_, (uint32_t)WSEC_NONE);
      EXPECT_EQ(auth_, (uint16_t)BRCMF_AUTH_MODE_OPEN);
      break;
    default:;
  }

  wlanif_assoc_req_t assoc_req = {.rsne_len = 0};

  if (sec_type_ == SEC_TYPE_WEP_SHARED104 || sec_type_ == SEC_TYPE_WEP_SHARED40 ||
      sec_type_ == SEC_TYPE_WEP_OPEN) {
    assoc_req.vendor_ie_len = 0;
  } else if (sec_type_ == SEC_TYPE_WPA1) {
    // construct a fake vendor ie in wlanif_assoc_req.
    uint16_t offset = 0;
    uint8_t* ie = (uint8_t*)assoc_req.vendor_ie;

    ie[offset++] = WLAN_IE_TYPE_VENDOR_SPECIFIC;
    ie[offset++] = 22;  // The length of following content.

    memcpy(&ie[offset], MSFT_OUI, TLV_OUI_LEN);
    offset += TLV_OUI_LEN;
    ie[offset++] = WPA_OUI_TYPE;

    // These two bytes are 16-bit version number.
    ie[offset++] = 1;  // Lower byte
    ie[offset++] = 0;  // Higher byte

    memcpy(&ie[offset], MSFT_OUI, TLV_OUI_LEN);
    offset += TLV_OUI_LEN;
    ie[offset++] = WPA_CIPHER_TKIP;  // Set multicast cipher suite.

    // These two bytes indicate the length of unicast cipher list, in this case is 1.
    ie[offset++] = 1;  // Lower byte
    ie[offset++] = 0;  // Higher byte

    memcpy(&ie[offset], MSFT_OUI, TLV_OUI_LEN);
    offset += TLV_OUI_LEN;               // The second WPA OUI.
    ie[offset++] = WPA_CIPHER_CCMP_128;  // Set unicast cipher suite.

    // These two bytes indicate the length of auth management suite list, in this case is 1.
    ie[offset++] = 1;  // Lower byte
    ie[offset++] = 0;  // Higher byte

    memcpy(&ie[offset], MSFT_OUI, TLV_OUI_LEN);  // WPA OUI for auth management suite.
    offset += TLV_OUI_LEN;
    ie[offset++] = RSN_AKM_PSK;  // Set auth management suite.

    assoc_req.vendor_ie_len = offset;
    ASSERT_EQ(assoc_req.vendor_ie_len, (const uint32_t)(ie[TLV_LEN_OFF] + TLV_HDR_LEN));
  } else if (sec_type_ == SEC_TYPE_WPA2) {
    // construct a fake rsne ie in wlanif_assoc_req.
    uint16_t offset = 0;
    uint8_t* ie = (uint8_t*)assoc_req.rsne;

    ie[offset++] = WLAN_IE_TYPE_RSNE;
    ie[offset++] = 20;  // The length of following content.

    // These two bytes are 16-bit version number.
    ie[offset++] = 1;  // Lower byte
    ie[offset++] = 0;  // Higher byte

    memcpy(&ie[offset], RSN_OUI, TLV_OUI_LEN);  // RSN OUI for multicast cipher suite.
    offset += TLV_OUI_LEN;
    ie[offset++] = WPA_CIPHER_TKIP;  // Set multicast cipher suite.

    // These two bytes indicate the length of unicast cipher list, in this case is 1.
    ie[offset++] = 1;  // Lower byte
    ie[offset++] = 0;  // Higher byte

    memcpy(&ie[offset], RSN_OUI, TLV_OUI_LEN);  // RSN OUI for unicast cipher suite.
    offset += TLV_OUI_LEN;
    ie[offset++] = WPA_CIPHER_CCMP_128;  // Set unicast cipher suite.

    // These two bytes indicate the length of auth management suite list, in this case is 1.
    ie[offset++] = 1;  // Lower byte
    ie[offset++] = 0;  // Higher byte

    memcpy(&ie[offset], RSN_OUI, TLV_OUI_LEN);  // RSN OUI for auth management suite.
    offset += TLV_OUI_LEN;
    ie[offset++] = RSN_AKM_PSK;  // Set auth management suite.

    // These two bytes indicate RSN capabilities, in this case is \x0c\x00.
    ie[offset++] = 12;  // Lower byte
    ie[offset++] = 0;   // Higher byte

    assoc_req.rsne_len = offset;
    ASSERT_EQ(assoc_req.rsne_len, (const uint32_t)(ie[TLV_LEN_OFF] + TLV_HDR_LEN));
  }

  memcpy(assoc_req.peer_sta_address, kDefaultBssid.byte, ETH_ALEN);
  client_ifc_.if_impl_ops_->assoc_req(client_ifc_.if_impl_ctx_, &assoc_req);
}

void AuthTest::OnAssocConf(const wlanif_assoc_confirm_t* resp) {
  assoc_status_ = resp->result_code;

  if (assoc_status_ != WLAN_ASSOC_RESULT_SUCCESS) {
    return;
  }

  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, client_ifc_.iface_id_);
  zx_status_t status;

  status = brcmf_fil_bsscfg_int_get(ifp, "wsec", &wsec_);
  EXPECT_EQ(status, ZX_OK);

  status = brcmf_fil_bsscfg_int_get(ifp, "wpa_auth", &wpa_auth_);
  EXPECT_EQ(status, ZX_OK);

  if (sec_type_ == SEC_TYPE_WPA1) {
    // The wsec iovar is set after sending assoc_req to driver.
    EXPECT_EQ(wsec_, (uint32_t)(TKIP_ENABLED | AES_ENABLED));
    EXPECT_EQ(wpa_auth_, (uint32_t)WPA_AUTH_PSK);
  }

  if (sec_type_ == SEC_TYPE_WPA2) {
    EXPECT_EQ(wsec_, (uint32_t)(TKIP_ENABLED | AES_ENABLED));
    EXPECT_EQ(wpa_auth_, (uint32_t)WPA2_AUTH_PSK);
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
  SCHEDULE_CALL(zx::msec(10), &AuthTest::StartAuth, this);

  env_->Run(kTestDuration);
  // The first SHARED KEY authentication request will fail, and switch to OPEN SYSTEM automatically.
  // OPEN SYSTEM authentication will succeed.
  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_SHARED_KEY, WLAN_AUTH_RESULT_SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_SHARED_KEY, WLAN_AUTH_RESULT_REFUSED);
  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_OPEN, WLAN_AUTH_RESULT_SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_OPEN, WLAN_AUTH_RESULT_SUCCESS);
  VerifyAuthFrames();
}

TEST_F(AuthTest, WEP40) {
  Init();
  sec_type_ = SEC_TYPE_WEP_SHARED40;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_SHARED_KEY,
                   .sec_type = simulation::SEC_PROTO_TYPE_WEP});
  SCHEDULE_CALL(zx::msec(10), &AuthTest::StartAuth, this);

  env_->Run(kTestDuration);
  // It should be a successful shared_key authentication
  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_SHARED_KEY, WLAN_AUTH_RESULT_SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_SHARED_KEY, WLAN_AUTH_RESULT_SUCCESS);
  expect_auth_frames_.emplace_back(3, simulation::AUTH_TYPE_SHARED_KEY, WLAN_AUTH_RESULT_SUCCESS);
  expect_auth_frames_.emplace_back(4, simulation::AUTH_TYPE_SHARED_KEY, WLAN_AUTH_RESULT_SUCCESS);
  VerifyAuthFrames();
}

TEST_F(AuthTest, WEPOPEN) {
  Init();
  sec_type_ = SEC_TYPE_WEP_OPEN;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_WEP});
  SCHEDULE_CALL(zx::msec(10), &AuthTest::StartAuth, this);

  env_->Run(kTestDuration);

  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_OPEN, WLAN_AUTH_RESULT_SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_OPEN, WLAN_AUTH_RESULT_SUCCESS);
  VerifyAuthFrames();
}

TEST_F(AuthTest, AuthFail) {
  Init();
  sec_type_ = SEC_TYPE_WEP_OPEN;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_OPEN});
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjIovar("auth", ZX_ERR_IO, client_ifc_.iface_id_);
  SCHEDULE_CALL(zx::msec(10), &AuthTest::StartAuth, this);

  env_->Run(kTestDuration);
  EXPECT_NE(auth_status_, WLAN_AUTH_RESULT_SUCCESS);
}

TEST_F(AuthTest, IgnoreTest) {
  Init();
  sec_type_ = SEC_TYPE_WEP_OPEN;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_WEP});
  ap_.SetAssocHandling(simulation::FakeAp::ASSOC_IGNORED);

  SCHEDULE_CALL(zx::msec(10), &AuthTest::StartAuth, this);

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
    expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_OPEN, WLAN_AUTH_RESULT_SUCCESS);
  }
  VerifyAuthFrames();
  EXPECT_EQ(assoc_status_, WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED);
}

TEST_F(AuthTest, WPA1Test) {
  Init();
  sec_type_ = SEC_TYPE_WPA1;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_WPA1});
  SCHEDULE_CALL(zx::msec(10), &AuthTest::StartAuth, this);

  env_->Run(kTestDuration);

  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_OPEN, WLAN_AUTH_RESULT_SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_OPEN, WLAN_AUTH_RESULT_SUCCESS);
  VerifyAuthFrames();
  // Make sure that OnAssocConf is called, so the check inside is called.
  EXPECT_EQ(assoc_status_, WLAN_ASSOC_RESULT_SUCCESS);
}

TEST_F(AuthTest, WPA1Fail) {
  Init();
  sec_type_ = SEC_TYPE_WPA1;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_WPA1});
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjIovar("wpaie", ZX_ERR_IO, client_ifc_.iface_id_);
  SCHEDULE_CALL(zx::msec(10), &AuthTest::StartAuth, this);

  env_->Run(kTestDuration);

  // Assoc should have failed
  EXPECT_NE(assoc_status_, WLAN_ASSOC_RESULT_SUCCESS);
}

TEST_F(AuthTest, WPA2Test) {
  Init();
  sec_type_ = SEC_TYPE_WPA2;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_WPA2});
  SCHEDULE_CALL(zx::msec(10), &AuthTest::StartAuth, this);

  env_->Run(kTestDuration);

  expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_OPEN, WLAN_AUTH_RESULT_SUCCESS);
  expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_OPEN, WLAN_AUTH_RESULT_SUCCESS);
  VerifyAuthFrames();
  // Make sure that OnAssocConf is called, so the check inside is called.
  EXPECT_EQ(assoc_status_, WLAN_ASSOC_RESULT_SUCCESS);
}

TEST_F(AuthTest, WPA2TestFail) {
  Init();
  sec_type_ = SEC_TYPE_WPA2;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_WPA2});
  SecErrorInject();
  SCHEDULE_CALL(zx::msec(10), &AuthTest::StartAuth, this);

  env_->Run(kTestDuration);
  // Make sure that OnAssocConf is called, so the check inside is called.
  EXPECT_NE(assoc_status_, WLAN_AUTH_RESULT_SUCCESS);
}

// This test case verifies that auth req will be refused when security types of client and AP are
// not matched.
TEST_F(AuthTest, WrongSecTypeAuthFail) {
  Init();
  // Client sec type is WPA1 while AP's is WEP.
  sec_type_ = SEC_TYPE_WPA1;
  ap_.SetSecurity({.auth_handling_mode = simulation::AUTH_TYPE_OPEN,
                   .sec_type = simulation::SEC_PROTO_TYPE_WEP});
  SCHEDULE_CALL(zx::msec(10), &AuthTest::StartAuth, this);

  env_->Run(kTestDuration);

  uint32_t max_retries = 0;

  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, client_ifc_.iface_id_);
  zx_status_t status = brcmf_fil_iovar_int_get(ifp, "assoc_retry_max", &max_retries, nullptr);
  EXPECT_EQ(status, ZX_OK);

  for (uint32_t i = 0; i < max_retries + 1; i++) {
    expect_auth_frames_.emplace_back(1, simulation::AUTH_TYPE_OPEN, WLAN_AUTH_RESULT_SUCCESS);
    expect_auth_frames_.emplace_back(2, simulation::AUTH_TYPE_OPEN, WLAN_AUTH_RESULT_REFUSED);
  }

  VerifyAuthFrames();
  EXPECT_EQ(assoc_status_, WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED);
}

}  // namespace wlan::brcmfmac
