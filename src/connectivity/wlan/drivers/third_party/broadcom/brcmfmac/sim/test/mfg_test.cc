// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/wlanif.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::brcmfmac {

constexpr uint16_t kDefaultCh = 149;
constexpr wlan_channel_t kDefaultChannel = {
    .primary = kDefaultCh, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
constexpr wlan_ssid_t kDefaultSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kFakeMac({0xde, 0xad, 0xbe, 0xef, 0x00, 0x02});
constexpr simulation::WlanTxInfo kDefaultTxInfo = {.channel = kDefaultChannel};

class MfgTest : public SimTest {
 public:
  static constexpr zx::duration kTestDuration = zx::sec(100);
  // How many devices have been registered by the fake devhost
  uint32_t DeviceCount();
  void CreateIF(wlan_info_mac_role_t role);
  void DelIF(SimInterface* ifc);
  void StartSoftAP();
  void TxAuthAndAssocReq();

 protected:
  SimInterface client_ifc_;
  SimInterface softap_ifc_;
};

uint32_t MfgTest::DeviceCount() { return (dev_mgr_->DeviceCount()); }

void MfgTest::CreateIF(wlan_info_mac_role_t role) {
  switch (role) {
    case WLAN_INFO_MAC_ROLE_CLIENT:
      ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_), ZX_OK);
      break;
    case WLAN_INFO_MAC_ROLE_AP:
      ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_), ZX_OK);
      break;
  }
}
void MfgTest::DelIF(SimInterface* ifc) { DeleteInterface(ifc); }

void MfgTest::StartSoftAP() {
  softap_ifc_.StartSoftAp(SimInterface::kDefaultSoftApSsid, kDefaultChannel);
}

void MfgTest::TxAuthAndAssocReq() {
  // Get the mac address of the SoftAP
  common::MacAddr soft_ap_mac;
  softap_ifc_.GetMacAddr(&soft_ap_mac);
  wlan_ssid_t ssid = {.len = 6, .ssid = "Sim_AP"};
  // Pass the auth stop for softAP iface before assoc.
  simulation::SimAuthFrame auth_req_frame(kFakeMac, soft_ap_mac, 1, simulation::AUTH_TYPE_OPEN,
                                          WLAN_STATUS_CODE_SUCCESS);
  env_->Tx(auth_req_frame, kDefaultTxInfo, this);
  simulation::SimAssocReqFrame assoc_req_frame(kFakeMac, soft_ap_mac, ssid);
  env_->Tx(assoc_req_frame, kDefaultTxInfo, this);
}

// Check to make sure only one IF can be active at anytime with MFG FW.
TEST_F(MfgTest, BasicTest) {
  Init();
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_), ZX_OK);

  // SoftAP If creation should fail as Client IF has already been created.
  ASSERT_NE(StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_, std::nullopt, kDefaultBssid),
            ZX_OK);

  // Now delete the Client IF and SoftAP creation should pass
  DeleteInterface(&client_ifc_);
  EXPECT_EQ(DeviceCount(), 1U);
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_, std::nullopt, kDefaultBssid),
            ZX_OK);
  // Now that SoftAP IF is created, Client IF creation should fail
  ASSERT_NE(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_), ZX_OK);
  DeleteInterface(&softap_ifc_);
}

// Start client and SoftAP interfaces and check if
// the client can associate to a FakeAP and a fake client can associate to the
// SoftAP.
TEST_F(MfgTest, CheckConnections) {
  Init();
  StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_);
  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);

  // Associate to FakeAp
  client_ifc_.AssociateWith(ap, zx::msec(10));
  SCHEDULE_CALL(zx::msec(100), &MfgTest::DelIF, this, &client_ifc_);
  SCHEDULE_CALL(zx::msec(200), &MfgTest::CreateIF, this, WLAN_INFO_MAC_ROLE_AP);
  SCHEDULE_CALL(zx::msec(300), &MfgTest::StartSoftAP, this);
  // Associate to SoftAP
  SCHEDULE_CALL(zx::msec(400), &MfgTest::TxAuthAndAssocReq, this);
  SCHEDULE_CALL(zx::msec(500), &MfgTest::DelIF, this, &softap_ifc_);

  env_->Run(kTestDuration);

  // Check if the client's assoc with FakeAP succeeded
  EXPECT_EQ(client_ifc_.stats_.assoc_attempts, 1U);
  EXPECT_EQ(client_ifc_.stats_.assoc_successes, 1U);
  // Deletion of the client IF should have resulted in disassoc of the
  // client (cleanup during IF delete).
  EXPECT_EQ(client_ifc_.stats_.disassoc_indications.size(), 1U);
  // Verify Assoc with SoftAP succeeded
  ASSERT_EQ(softap_ifc_.stats_.assoc_indications.size(), 1U);
  ASSERT_EQ(softap_ifc_.stats_.auth_indications.size(), 1U);
}
}  // namespace wlan::brcmfmac
