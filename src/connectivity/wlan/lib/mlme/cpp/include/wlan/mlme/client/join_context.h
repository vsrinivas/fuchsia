// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_JOIN_CONTEXT_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_JOIN_CONTEXT_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <zircon/types.h>

#include <ddk/hw/wlan/wlaninfo.h>
#include <wlan/common/channel.h>
#include <wlan/common/logging.h>
#include <wlan/common/macaddr.h>

namespace wlan {

class JoinContext {
 public:
  JoinContext(::fuchsia::wlan::mlme::BSSDescription bss, ::fuchsia::wlan::common::PHY phy,
              ::fuchsia::wlan::common::CBW cbw);

  const common::MacAddr& bssid() const { return bssid_; }
  const wlan_channel_t& channel() const { return channel_; }
  const wlan_channel_t& bss_channel() const { return bss_channel_; }
  uint16_t listen_interval() const { return listen_interval_; }
  void set_listen_interval(uint16_t listen_interval) { listen_interval_ = listen_interval; }

  wlan_info_phy_type_t phy() const { return phy_; }
  const ::fuchsia::wlan::mlme::BSSDescription* bss() const { return &bss_; }

  bool IsHt() const { return phy_ == WLAN_INFO_PHY_TYPE_HT; }
  bool IsVht() const { return phy_ == WLAN_INFO_PHY_TYPE_VHT; }

  // SanitizeChannel tests the validation of input wlan_channel_t
  // to support interoperable Join and Association.
  // This provides a defensive meature to inconsistent
  // announcement from the neighbor BSS, and potential ignorance of SME.
  static wlan_channel_t SanitizeChannel(const wlan_channel_t& chan);

 private:
  ::fuchsia::wlan::mlme::BSSDescription bss_;
  wlan_channel_t channel_;
  wlan_channel_t bss_channel_;
  wlan_info_phy_type_t phy_;
  common::MacAddr bssid_;

  // TODO(NET-1819): Redesign AssocContext and move this there.
  uint16_t listen_interval_ = 0;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_JOIN_CONTEXT_H_
