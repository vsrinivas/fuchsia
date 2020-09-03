// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/wlanif.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::brcmfmac {

// Some default AP and association request values
constexpr uint16_t kDefaultCh = 149;
constexpr wlan_channel_t kDefaultChannel = {
    .primary = kDefaultCh, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
// Chanspec value corresponding to kDefaultChannel with current d11 encoder.
constexpr uint16_t kDefaultChanspec = 53397;
constexpr uint16_t kTestChanspec = 0xd0a5;
constexpr uint16_t kTest1Chanspec = 0xd095;
constexpr simulation::WlanTxInfo kDefaultTxInfo = {.channel = kDefaultChannel};
constexpr wlan_ssid_t kDefaultSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
const common::MacAddr kFakeMac({0xde, 0xad, 0xbe, 0xef, 0x00, 0x02});
const char kFakeClientName[] = "fake-client-iface";
const char kFakeApName[] = "fake-ap-iface";

class DynamicIfTest : public SimTest {
 public:
  // How long an individual test will run for. We need an end time because tests run until no more
  // events remain and if ap's are beaconing the test will run indefinitely.
  static constexpr zx::duration kTestDuration = zx::sec(100);
  DynamicIfTest() = default;

  // How many devices have been registered by the fake devhost
  uint32_t DeviceCount();

  // Force fail an attempt to stop the soft AP
  void InjectStopAPError();

  // Verify SoftAP channel followed client channel
  void ChannelCheck();

  // Generate an association request to send to the soft AP
  void TxAuthAndAssocReq();
  void VerifyAssocWithSoftAP();

  // Query for wlanphy info
  void PhyQuery(wlanphy_impl_info_t* out_info);

  // Interfaces to set and get chanspec iovar in sim-fw
  void SetChanspec(bool is_ap_iface, uint16_t* chanspec, zx_status_t expect_result);
  uint16_t GetChanspec(bool is_ap_iface, zx_status_t expect_result);

  // Run a dual mode (apsta) test, verifying AP stop behavior
  void TestApStop(bool use_cdown);

 protected:
  SimInterface client_ifc_;
  SimInterface softap_ifc_;
  void CheckAddIfaceWritesWdev(wlan_info_mac_role_t role, const char iface_name[],
                               SimInterface& ifc);
};

void DynamicIfTest::PhyQuery(wlanphy_impl_info_t* out_info) {
  zx_status_t status;
  status = device_->WlanphyImplQuery(out_info);
  ASSERT_EQ(status, ZX_OK);
}

uint32_t DynamicIfTest::DeviceCount() { return (dev_mgr_->DeviceCount()); }

void DynamicIfTest::InjectStopAPError() {
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjIovar("bss", ZX_ERR_IO, softap_ifc_.iface_id_);
}

void DynamicIfTest::ChannelCheck() {
  uint16_t softap_chanspec = GetChanspec(true, ZX_OK);
  uint16_t client_chanspec = GetChanspec(false, ZX_OK);
  EXPECT_EQ(softap_chanspec, client_chanspec);
  brcmf_simdev* sim = device_->GetSim();
  wlan_channel_t chan;
  sim->sim_fw->convert_chanspec_to_channel(softap_chanspec, &chan);
  EXPECT_EQ(softap_ifc_.stats_.csa_indications.size(), 1U);
  EXPECT_EQ(chan.primary, softap_ifc_.stats_.csa_indications.front().new_channel);
}

void DynamicIfTest::TxAuthAndAssocReq() {
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

void DynamicIfTest::VerifyAssocWithSoftAP() {
  // Verify the event indications were received and
  // the number of clients
  ASSERT_EQ(softap_ifc_.stats_.assoc_indications.size(), 1U);
  ASSERT_EQ(softap_ifc_.stats_.auth_indications.size(), 1U);
  brcmf_simdev* sim = device_->GetSim();
  uint16_t num_clients = sim->sim_fw->GetNumClients(softap_ifc_.iface_id_);
  ASSERT_EQ(num_clients, 1U);
}

void DynamicIfTest::SetChanspec(bool is_ap_iface, uint16_t* chanspec, zx_status_t expect_result) {
  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp =
      brcmf_get_ifp(sim->drvr, is_ap_iface ? softap_ifc_.iface_id_ : client_ifc_.iface_id_);
  zx_status_t status = brcmf_fil_iovar_int_set(ifp, "chanspec", *chanspec, nullptr);
  EXPECT_EQ(status, expect_result);
}

uint16_t DynamicIfTest::GetChanspec(bool is_ap_iface, zx_status_t expect_result) {
  brcmf_simdev* sim = device_->GetSim();
  uint32_t chanspec;
  struct brcmf_if* ifp =
      brcmf_get_ifp(sim->drvr, is_ap_iface ? softap_ifc_.iface_id_ : client_ifc_.iface_id_);
  zx_status_t status = brcmf_fil_iovar_int_get(ifp, "chanspec", &chanspec, nullptr);
  EXPECT_EQ(status, expect_result);
  return chanspec;
}

TEST_F(DynamicIfTest, CreateDestroy) {
  Init();

  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_), ZX_OK);
  DeleteInterface(client_ifc_);
  EXPECT_EQ(DeviceCount(), 1U);

  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_, std::nullopt, kDefaultBssid),
            ZX_OK);

  // Verify whether the default bssid is correctly set to sim-fw when creating softAP iface.
  common::MacAddr soft_ap_mac;
  softap_ifc_.GetMacAddr(&soft_ap_mac);
  EXPECT_EQ(soft_ap_mac, kDefaultBssid);

  DeleteInterface(softap_ifc_);
  EXPECT_EQ(DeviceCount(), 1U);
}

// This test case verifies that starting an AP iface using the same MAC address as the existing
// client iface will return an error.
TEST_F(DynamicIfTest, CreateAPwithSameMacAsClient) {
  Init();
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_), ZX_OK);

  // Create AP iface with the same mac addr.
  common::MacAddr client_mac;
  client_ifc_.GetMacAddr(&client_mac);
  EXPECT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_, std::nullopt, client_mac),
            ZX_ERR_ALREADY_EXISTS);
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  DeleteInterface(client_ifc_);
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));
}

// This test verifies that if we want to create an client iface with the same MAC address as the
// pre-set one, no error will be returned.
TEST_F(DynamicIfTest, CreateClientwithPreAllocMac) {
  Init();
  common::MacAddr pre_set_mac;
  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, 0);
  zx_status_t status =
      brcmf_fil_iovar_data_get(ifp, "cur_etheraddr", pre_set_mac.byte, ETH_ALEN, nullptr);
  EXPECT_EQ(status, ZX_OK);

  EXPECT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_, std::nullopt, pre_set_mac),
            ZX_OK);
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(2));
  DeleteInterface(client_ifc_);
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));
}

// This test verifies brcmf_cfg80211_add_iface() returns ZX_ERR_INVALID_ARGS if the wdev_out
// argument is nullptr.
TEST_F(DynamicIfTest, CreateIfaceMustProvideWdevOut) {
  Init();
  brcmf_simdev* sim = device_->GetSim();

  wlan_info_mac_role_t client_role = WLAN_INFO_MAC_ROLE_CLIENT;
  EXPECT_EQ(ZX_OK, client_ifc_.Init(env_, client_role));
  wlanphy_impl_create_iface_req_t req = {
      .role = client_role,
      .sme_channel = client_ifc_.ch_mlme_,
      .has_init_mac_addr = false,
  };
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            brcmf_cfg80211_add_iface(sim->drvr, kFakeClientName, nullptr, &req, nullptr));

  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));
}

void DynamicIfTest::CheckAddIfaceWritesWdev(wlan_info_mac_role_t role, const char iface_name[],
                                            SimInterface& ifc) {
  brcmf_simdev* sim = device_->GetSim();
  wireless_dev* wdev = nullptr;

  EXPECT_EQ(ZX_OK, ifc.Init(env_, role));
  wlanphy_impl_create_iface_req_t req = {
      .role = role,
      .sme_channel = ifc.ch_mlme_,
      .has_init_mac_addr = false,
  };
  EXPECT_EQ(ZX_OK, brcmf_cfg80211_add_iface(sim->drvr, iface_name, nullptr, &req, &wdev));
  EXPECT_NE(nullptr, wdev);
  EXPECT_NE(nullptr, wdev->netdev);
  EXPECT_EQ(wdev->iftype, role);

  EXPECT_EQ(ZX_OK, brcmf_cfg80211_del_iface(sim->drvr->config, wdev));

  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));
}

// This test verifies brcmf_cfg80211_add_iface() behavior with respect to
// the wdev_out argument and the client role.
TEST_F(DynamicIfTest, CreateClientWritesWdev) {
  Init();
  CheckAddIfaceWritesWdev(WLAN_INFO_MAC_ROLE_CLIENT, kFakeClientName, client_ifc_);
}

// This test verifies brcmf_cfg80211_add_iface() behavior with respect to
// the wdev_out argument and the AP role.
TEST_F(DynamicIfTest, CreateApWritesWdev) {
  Init();
  CheckAddIfaceWritesWdev(WLAN_INFO_MAC_ROLE_AP, kFakeApName, softap_ifc_);
}

// This test verifies new client interface names are assigned, and that the default for the
// primary network interface is kPrimaryNetworkInterfaceName (defined in core.h)
TEST_F(DynamicIfTest, CreateClientWithCustomName) {
  Init();
  brcmf_simdev* sim = device_->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, 0);
  wireless_dev* wdev = nullptr;

  wlan_info_mac_role_t client_role = WLAN_INFO_MAC_ROLE_CLIENT;
  EXPECT_EQ(ZX_OK, client_ifc_.Init(env_, client_role));

  wlanphy_impl_create_iface_req_t req = {
      .role = client_role,
      .sme_channel = client_ifc_.ch_mlme_,
      .has_init_mac_addr = false,
  };
  EXPECT_EQ(0, strcmp(brcmf_ifname(ifp), kPrimaryNetworkInterfaceName));
  EXPECT_EQ(ZX_OK, brcmf_cfg80211_add_iface(sim->drvr, kFakeClientName, nullptr, &req, &wdev));
  EXPECT_EQ(0, strcmp(wdev->netdev->name, kFakeClientName));
  EXPECT_EQ(0, strcmp(brcmf_ifname(ifp), kFakeClientName));
  EXPECT_EQ(ZX_OK, brcmf_cfg80211_del_iface(sim->drvr->config, wdev));
  EXPECT_EQ(0, strcmp(brcmf_ifname(ifp), kPrimaryNetworkInterfaceName));

  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));
}

// This test verifies new ap interface names are assigned.
TEST_F(DynamicIfTest, CreateApWithCustomName) {
  Init();
  brcmf_simdev* sim = device_->GetSim();
  wireless_dev* wdev = nullptr;

  wlan_info_mac_role_t ap_role = WLAN_INFO_MAC_ROLE_AP;
  EXPECT_EQ(ZX_OK, softap_ifc_.Init(env_, ap_role));

  wlanphy_impl_create_iface_req_t req = {
      .role = ap_role,
      .sme_channel = softap_ifc_.ch_mlme_,
      .has_init_mac_addr = false,
  };
  EXPECT_EQ(ZX_OK, brcmf_cfg80211_add_iface(sim->drvr, kFakeApName, nullptr, &req, &wdev));
  EXPECT_EQ(0, strcmp(wdev->netdev->name, kFakeApName));
  EXPECT_EQ(ZX_OK, brcmf_cfg80211_del_iface(sim->drvr->config, wdev));

  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));
}

// This test verifies the truncation of long interface names.
TEST_F(DynamicIfTest, CreateClientWithLongName) {
  Init();
  brcmf_simdev* sim = device_->GetSim();
  wireless_dev* wdev = nullptr;

  wlan_info_mac_role_t client_role = WLAN_INFO_MAC_ROLE_CLIENT;
  EXPECT_EQ(ZX_OK, client_ifc_.Init(env_, client_role));

  size_t really_long_name_len = NET_DEVICE_NAME_MAX_LEN + 1;
  ASSERT_GT(really_long_name_len,
            (size_t)NET_DEVICE_NAME_MAX_LEN);  // assert + 1 did not cause an overflow
  char really_long_name[really_long_name_len];
  for (size_t i = 0; i < really_long_name_len - 1; i++) {
    really_long_name[i] = '0' + ((i + 1) % 10);
  }
  really_long_name[really_long_name_len - 1] = '\0';

  char truncated_name[NET_DEVICE_NAME_MAX_LEN];
  strlcpy(truncated_name, really_long_name, sizeof(truncated_name));
  ASSERT_LT(strlen(truncated_name),
            strlen(really_long_name));  // sanity check that truncated_name is actually shorter

  wlanphy_impl_create_iface_req_t req = {
      .role = client_role,
      .sme_channel = client_ifc_.ch_mlme_,
      .has_init_mac_addr = false,
  };
  EXPECT_EQ(ZX_OK, brcmf_cfg80211_add_iface(sim->drvr, really_long_name, nullptr, &req, &wdev));
  EXPECT_EQ(0, strcmp(wdev->netdev->name, truncated_name));
  EXPECT_EQ(ZX_OK, brcmf_cfg80211_del_iface(sim->drvr->config, wdev));

  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));
}

TEST_F(DynamicIfTest, DualInterfaces) {
  Init();
  StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_);
  StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_);
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(3));

  DeleteInterface(client_ifc_);
  DeleteInterface(softap_ifc_);
  EXPECT_EQ(DeviceCount(), static_cast<size_t>(1));
}

TEST_F(DynamicIfTest, PhyQuery) {
  Init();
  StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_);
  // Test brcmfmac supports simutaneous client ap operation
  wlanphy_impl_info_t info = {};
  PhyQuery(&info);
  EXPECT_NE(info.wlan_info.caps & WLAN_INFO_HARDWARE_CAPABILITY_SIMULTANEOUS_CLIENT_AP,
            static_cast<size_t>(0));
}

// Start both client and SoftAP interfaces simultaneously and check if
// the client can associate to a FakeAP and a fake client can associate to the
// SoftAP.
TEST_F(DynamicIfTest, ConnectBothInterfaces) {
  // Create our device instances
  Init();
  StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_);
  StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_);

  // Start our SoftAP
  softap_ifc_.StartSoftAp();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);

  // Associate to FakeAp
  client_ifc_.AssociateWith(ap, zx::msec(10));
  // Associate to SoftAP
  SCHEDULE_CALL(zx::msec(100), &DynamicIfTest::TxAuthAndAssocReq, this);

  env_->Run(kTestDuration);

  // Check if the client's assoc with FakeAP succeeded
  EXPECT_EQ(client_ifc_.stats_.assoc_attempts, 1U);
  EXPECT_EQ(client_ifc_.stats_.assoc_successes, 1U);
  // Verify Assoc with SoftAP succeeded
  VerifyAssocWithSoftAP();
  // TODO(karthikrish) Will add disassoc once support in SIM FW is available
}

void DynamicIfTest::TestApStop(bool use_cdown) {
  // Create our device instances
  Init();
  StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_);
  StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_);

  // Start our SoftAP
  softap_ifc_.StartSoftAp();

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(100));

  // Optionally force the use of a C_DOWN command, which has the side-effect of bringing down the
  // client interface.
  if (use_cdown) {
    InjectStopAPError();
  }

  // Associate to FakeAp
  client_ifc_.AssociateWith(ap, zx::msec(10));

  // Associate to SoftAP
  SCHEDULE_CALL(zx::msec(100), &DynamicIfTest::TxAuthAndAssocReq, this);

  // Verify Assoc with SoftAP succeeded
  SCHEDULE_CALL(zx::msec(150), &DynamicIfTest::VerifyAssocWithSoftAP, this);
  SCHEDULE_CALL(zx::msec(160), &SimInterface::StopSoftAp, &softap_ifc_);

  env_->Run(kTestDuration);

  // Check if the client's assoc with FakeAP succeeded
  EXPECT_EQ(client_ifc_.stats_.assoc_attempts, 1U);
  EXPECT_EQ(client_ifc_.stats_.assoc_successes, 1U);
  // Disassoc and other assoc scenarios are covered in assoc_test.cc
}

// Start both client and SoftAP interfaces simultaneously and check if stopping the AP's beacons
// does not affect the client.
TEST_F(DynamicIfTest, StopAPDoesntAffectClientIF) {
  TestApStop(false);
  // Verify that we didn't shut down our client interface
  EXPECT_EQ(client_ifc_.stats_.deauth_indications.size(), 0U);
}

// Start both client and SoftAP interfaces simultaneously and check if stopping the AP with iovar
// bss fail, brings down the client as well because C_DOWN is issued
TEST_F(DynamicIfTest, UsingCdownDisconnectsClient) {
  TestApStop(true);
  // Verify that the client interface was also shut down
  EXPECT_EQ(client_ifc_.stats_.deauth_indications.size(), 1U);
}

TEST_F(DynamicIfTest, SetClientChanspecAfterAPStarted) {
  // Create our device instances
  Init();

  uint16_t chanspec;
  // Create softAP iface and start
  StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_);
  softap_ifc_.StartSoftAp(SimInterface::kDefaultSoftApSsid, kDefaultChannel);

  // The chanspec of softAP iface should be set to default one.
  chanspec = GetChanspec(true, ZX_OK);
  EXPECT_EQ(chanspec, kDefaultChanspec);

  // After creating client iface and setting a different chanspec to it, chanspec of softAP will
  // change as a result of this operation.
  StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_);
  chanspec = kTestChanspec;
  SetChanspec(false, &chanspec, ZX_OK);

  // Confirm chanspec of AP is same as client
  chanspec = GetChanspec(true, ZX_OK);
  EXPECT_EQ(chanspec, kTestChanspec);
}

TEST_F(DynamicIfTest, SetAPChanspecAfterClientCreated) {
  // Create our device instances
  Init();

  // Create client iface and set chanspec
  uint16_t chanspec = kTestChanspec;
  StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_);
  SetChanspec(false, &chanspec, ZX_OK);

  // Create and start softAP iface to and set another chanspec
  StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_);
  softap_ifc_.StartSoftAp();
  // When we call StartSoftAP, the kDefaultCh will be transformed into chanspec(in this case the
  // value is 53397) and set to softAP iface, but since there is already a client iface activated,
  // that input chanspec will be ignored and set to client's chanspec.
  chanspec = GetChanspec(true, ZX_OK);
  EXPECT_EQ(chanspec, kTestChanspec);

  // Now if we set chanspec again to softAP when it already have a chanspec, this operation is
  // silently rejected
  chanspec = kTest1Chanspec;
  SetChanspec(true, &chanspec, ZX_OK);
}

// Start SoftAP after client assoc. SoftAP's channel should get set to client's channel
TEST_F(DynamicIfTest, CheckSoftAPChannel) {
  // Create our device instances
  Init();
  StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_);
  StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_);

  // Start up our fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);

  zx::duration delay = zx::msec(10);
  // Associate to FakeAp
  client_ifc_.AssociateWith(ap, delay);
  // Start our SoftAP
  delay += zx::msec(10);
  SCHEDULE_CALL(delay, &SimInterface::StartSoftAp, &softap_ifc_, SimInterface::kDefaultSoftApSsid,
                kDefaultChannel, 100, 100);

  // Wait until SIM FW sends AP Start confirmation. This is set as a
  // scheduled event to ensure test runs until AP Start confirmation is
  // received.
  delay += kStartAPConfDelay + zx::msec(10);
  SCHEDULE_CALL(delay, &DynamicIfTest::ChannelCheck, this);
  env_->Run(kTestDuration);

  EXPECT_EQ(client_ifc_.stats_.assoc_successes, 1U);
}
}  // namespace wlan::brcmfmac
