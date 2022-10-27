// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/hardware/wlan/associnfo/c/banjo.h>
#include <fuchsia/hardware/wlan/fullmac/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <zircon/errors.h>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/macaddr.h"

namespace wlan::brcmfmac {

static constexpr zx::duration kTestDuration = zx::sec(100);
static constexpr auto kDisassocReason = ::fuchsia::wlan::ieee80211::ReasonCode::NOT_AUTHENTICATED;
static const common::MacAddr kApBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
static constexpr cssid_t kApSsid = {.len = 15, .data = "Fuchsia Fake AP"};
static constexpr wlan_channel_t kApChannel = {
    .primary = 9, .cbw = CHANNEL_BANDWIDTH_CBW20, .secondary80 = 0};
static const common::MacAddr kStaMacAddr({0x11, 0x22, 0x33, 0x44, 0x55, 0x66});

TEST_F(SimTest, Disassoc) {
  simulation::FakeAp ap(env_.get(), kApBssid, kApSsid, kApChannel);

  ASSERT_EQ(Init(), ZX_OK);

  SimInterface client_ifc;
  ASSERT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &client_ifc, std::nullopt, kStaMacAddr), ZX_OK);

  client_ifc.AssociateWith(ap, zx::sec(1));
  env_->ScheduleNotification(
      std::bind(&simulation::FakeAp::DisassocSta, &ap, kStaMacAddr, kDisassocReason), zx::sec(2));

  env_->Run(kTestDuration);

  // Make sure association was successful
  ASSERT_EQ(client_ifc.stats_.connect_attempts, 1U);
  ASSERT_EQ(client_ifc.stats_.connect_results.size(), 1U);
  ASSERT_EQ(client_ifc.stats_.connect_results.front().result_code, STATUS_CODE_SUCCESS);

  // Make sure disassociation was successful
  EXPECT_EQ(ap.GetNumAssociatedClient(), 0U);

  // Verify that we get appropriate notification
  ASSERT_EQ(client_ifc.stats_.disassoc_indications.size(), 1U);
  const wlan_fullmac_disassoc_indication_t& disassoc_ind =
      client_ifc.stats_.disassoc_indications.front();
  // Verify reason code is propagated
  EXPECT_EQ(disassoc_ind.reason_code, static_cast<reason_code_t>(kDisassocReason));
  // Disassociated by AP so not locally initiated
  EXPECT_EQ(disassoc_ind.locally_initiated, false);
}

// Verify that we properly track the disconnect mode which indicates if a disconnect was initiated
// by SME or not and what kind of disconnect it is. If this is not properly handled we could end up
// in a state where we are disconnected but SME doesn't know about it.
TEST_F(SimTest, SmeDeauthFollowedByFwDisassoc) {
  simulation::FakeAp ap(env_.get(), kApBssid, kApSsid, kApChannel);

  ASSERT_EQ(Init(), ZX_OK);

  SimInterface client_ifc;
  ASSERT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &client_ifc, std::nullopt, kStaMacAddr), ZX_OK);

  client_ifc.AssociateWith(ap, zx::sec(1));
  constexpr reason_code_t deauth_reason = REASON_CODE_LEAVING_NETWORK_DISASSOC;
  // Schedule a deauth from SME
  env_->ScheduleNotification([&] { client_ifc.DeauthenticateFrom(kApBssid, deauth_reason); },
                             zx::sec(2));
  // Associate again
  client_ifc.AssociateWith(ap, zx::sec(3));
  // Schedule a disassocaition from firmware
  ::fuchsia::wlan::ieee80211::ReasonCode disassoc_reason =
      ::fuchsia::wlan::ieee80211::ReasonCode::UNSPECIFIED_REASON;
  SimFirmware& fw = *device_->GetSim()->sim_fw;
  // Note that this disassociation cannot go through SME, it has to be initiated by firmware so that
  // the disconnect mode tracking is not modified.
  env_->ScheduleNotification([&] { fw.TriggerFirmwareDisassoc(disassoc_reason); }, zx::sec(4));

  env_->Run(kTestDuration);

  // Make sure associations were successful
  ASSERT_EQ(client_ifc.stats_.connect_attempts, 2U);
  ASSERT_EQ(client_ifc.stats_.connect_results.size(), 2U);
  ASSERT_EQ(client_ifc.stats_.connect_results.front().result_code, STATUS_CODE_SUCCESS);
  ASSERT_EQ(client_ifc.stats_.connect_results.back().result_code, STATUS_CODE_SUCCESS);

  // Make sure disassociation was successful
  EXPECT_EQ(ap.GetNumAssociatedClient(), 0U);

  // Verify that we got the deauth confirmation
  ASSERT_EQ(client_ifc.stats_.deauth_results.size(), 1U);
  const wlan_fullmac_deauth_confirm_t& deauth_confirm = client_ifc.stats_.deauth_results.front();
  EXPECT_EQ(0, memcmp(deauth_confirm.peer_sta_address, kApBssid.byte, ETH_ALEN));

  // Verify that we got the disassociation indication, not a confirmation or anything else
  ASSERT_EQ(client_ifc.stats_.disassoc_indications.size(), 1U);
  const wlan_fullmac_disassoc_indication_t& disassoc_ind =
      client_ifc.stats_.disassoc_indications.front();
  EXPECT_EQ(disassoc_ind.reason_code, static_cast<reason_code_t>(disassoc_reason));
  EXPECT_EQ(disassoc_ind.locally_initiated, true);
}

}  // namespace wlan::brcmfmac
