// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-frame.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/macaddr.h"

namespace wlan::brcmfmac {

// Some default AP and association request values
constexpr cssid_t kDefaultSsid = {.len = 15, .data = "Fuchsia Fake AP"};

const wlan_channel_t kAp0Channel = {.primary = 9, .cbw = CHANNEL_BANDWIDTH_CBW20, .secondary80 = 0};
const wlan_channel_t kAp1Channel = {
    .primary = 11, .cbw = CHANNEL_BANDWIDTH_CBW20, .secondary80 = 0};
const simulation::WlanTxInfo kAp0TxInfo = {.channel = kAp0Channel};

const common::MacAddr kAp0Bssid("12:34:56:78:9a:bc");
const common::MacAddr kAp1Bssid("ff:ee:dd:cc:bb:aa");

class ReassocTest : public SimTest {
 public:
  // How long an individual test will run for. We need an end time because tests run until no more
  // events remain and so we need to stop aps from beaconing to drain the event queue.
  static constexpr zx::duration kTestDuration = zx::sec(100);

  void Init();

  // Schedule a future reassoc response event.
  void ScheduleReassocResp(const simulation::SimReassocRespFrame& reassoc_resp, zx::duration when);

 protected:
  // This is the interface we will use for our single client interface
  SimInterface client_ifc_;
  std::list<simulation::FakeAp*> aps_;
};

// Create our device instance and hook up the callbacks
void ReassocTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &client_ifc_), ZX_OK);
}

// This function schedules a reassoc response frame sent from an AP.
void ReassocTest::ScheduleReassocResp(const simulation::SimReassocRespFrame& reassoc_resp,
                                      zx::duration when) {
  env_->ScheduleNotification([this, reassoc_resp] { env_->Tx(reassoc_resp, kAp0TxInfo, this); },
                             when);
}

TEST_F(ReassocTest, IgnoreSpuriousReassocResp) {
  Init();

  simulation::FakeAp ap_0(env_.get(), kAp0Bssid, kDefaultSsid, kAp0Channel);
  simulation::FakeAp ap_1(env_.get(), kAp1Bssid, kDefaultSsid, kAp1Channel);
  ap_0.EnableBeacon(zx::msec(60));
  ap_1.EnableBeacon(zx::msec(60));
  aps_.push_back(&ap_0);
  aps_.push_back(&ap_1);

  client_ifc_.AssociateWith(ap_0, zx::msec(10));

  common::MacAddr client_mac;
  client_ifc_.GetMacAddr(&client_mac);
  // Intentionally create a response frame that never had a corresponding request.
  simulation::SimReassocRespFrame reassoc_resp(kAp0Bssid, client_mac,
                                               ::fuchsia::wlan::ieee80211::StatusCode::SUCCESS);
  ScheduleReassocResp(reassoc_resp, zx::sec(1));
  env_->Run(kTestDuration);

  EXPECT_EQ(SimInterface::AssocContext::kAssociated, client_ifc_.assoc_ctx_.state);
  EXPECT_EQ(kAp0Bssid, client_ifc_.assoc_ctx_.bssid);
  EXPECT_EQ(1U, ap_0.GetNumAssociatedClient());
  EXPECT_EQ(0U, ap_1.GetNumAssociatedClient());
}

}  // namespace wlan::brcmfmac
