// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/wlanif/test/test_bss.h"

#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>
#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <fuchsia/wlan/internal/cpp/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/channel.h"
#include "src/connectivity/wlan/lib/mlme/cpp/include/wlan/mlme/mac_frame.h"

namespace wlan_fullmac_test {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_ieee80211 = ::fuchsia::wlan::ieee80211;
namespace wlan_internal = ::fuchsia::wlan::internal;
namespace wlan_mlme = ::fuchsia::wlan::mlme;

wlan_internal::BssDescription CreateBssDescription(wlan_channel_t channel) {
  wlan::common::MacAddr bssid(kBssid1);

  wlan_internal::BssDescription bss_desc;
  std::memcpy(bss_desc.bssid.data(), bssid.byte, wlan::common::kMacAddrLen);
  std::vector<uint8_t> ssid(kSsid, kSsid + sizeof(kSsid));
  bss_desc.bss_type = wlan_internal::BssType::INFRASTRUCTURE;
  bss_desc.beacon_period = kBeaconPeriodTu;
  bss_desc.capability_info = 1 | 1 << 5;  // ESS and short preamble bits
  bss_desc.ies = std::vector<uint8_t>(kIes, kIes + sizeof(kIes));
  bss_desc.channel.cbw = static_cast<wlan_common::ChannelBandwidth>(channel.cbw);
  bss_desc.channel.primary = channel.primary;

  bss_desc.rssi_dbm = -35;

  return bss_desc;
}

wlan_mlme::StartRequest CreateStartReq() {
  wlan_mlme::StartRequest req;
  std::vector<uint8_t> ssid(kSsid, kSsid + sizeof(kSsid));
  req.ssid = std::move(ssid);
  req.bss_type = wlan_internal::BssType::INFRASTRUCTURE;
  req.beacon_period = kBeaconPeriodTu;
  req.dtim_period = kDtimPeriodTu;
  req.channel = kBssChannel.primary;
  req.rates = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c};
  req.mesh_id.resize(0);
  req.phy = wlan_common::WlanPhyType::ERP;
  req.rsne.emplace(std::vector<uint8_t>(kRsne, kRsne + sizeof(kRsne)));
  return req;
}

wlan_mlme::StopRequest CreateStopReq() {
  wlan_mlme::StopRequest req;
  req.ssid = std::vector<uint8_t>(kSsid, kSsid + sizeof(kSsid));
  return req;
}

wlan_mlme::JoinRequest CreateJoinReq() {
  wlan_mlme::JoinRequest req;
  req.selected_bss = CreateBssDescription();
  req.join_failure_timeout = kJoinTimeout;
  req.nav_sync_delay = 20;
  req.op_rates = {12, 24, 48};
  return req;
}

wlan_mlme::AuthenticateRequest CreateAuthenticateReq() {
  wlan::common::MacAddr bssid(kBssid1);
  wlan_mlme::AuthenticateRequest req;
  std::memcpy(req.peer_sta_address.data(), bssid.byte, wlan::common::kMacAddrLen);
  req.auth_failure_timeout = kAuthTimeout;
  req.auth_type = wlan_mlme::AuthenticationTypes::OPEN_SYSTEM;
  return req;
}

wlan_mlme::DeauthenticateRequest CreateDeauthenticateReq() {
  wlan::common::MacAddr bssid(kBssid1);
  wlan_mlme::DeauthenticateRequest req;
  std::memcpy(req.peer_sta_address.data(), bssid.byte, wlan::common::kMacAddrLen);
  req.reason_code = wlan_ieee80211::ReasonCode::UNSPECIFIED_REASON;
  return req;
}

wlan_mlme::AssociateRequest CreateAssociateReq() {
  wlan::common::MacAddr bssid(kBssid1);
  wlan_mlme::AssociateRequest req;
  std::memcpy(req.peer_sta_address.data(), bssid.byte, wlan::common::kMacAddrLen);
  req.rates = {std::cbegin(kRates), std::cend(kRates)};
  req.rsne.emplace(std::vector<uint8_t>(kRsne, kRsne + sizeof(kRsne)));
  return req;
}

wlan_mlme::DisassociateRequest CreateDisassociateReq() {
  wlan::common::MacAddr bssid(kBssid1);
  wlan_mlme::DisassociateRequest req;
  std::memcpy(req.peer_sta_address.data(), bssid.byte, wlan::common::kMacAddrLen);
  req.reason_code = wlan_ieee80211::ReasonCode::UNSPECIFIED_REASON;
  return req;
}

}  // namespace wlan_fullmac_test
