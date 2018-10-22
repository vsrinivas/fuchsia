// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_CLIENT_JOIN_CONTEXT_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_CLIENT_JOIN_CONTEXT_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/common/channel.h>
#include <wlan/common/logging.h>
#include <wlan/common/macaddr.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

namespace wlan {

class JoinContext {
   public:
    JoinContext(::fuchsia::wlan::mlme::BSSDescription bss, ::fuchsia::wlan::mlme::PHY phy,
                ::fuchsia::wlan::mlme::CBW cbw);

    const common::MacAddr& bssid() const { return bssid_; }
    const wlan_channel_t& channel() const { return channel_; }
    const wlan_channel_t& bss_channel() const { return bss_channel_; }
    const ::fuchsia::wlan::mlme::PHY& phy() const { return phy_; }
    const ::fuchsia::wlan::mlme::BSSDescription* bss() const { return &bss_; }

    bool IsHtOrLater() const {
        return (phy_ == ::fuchsia::wlan::mlme::PHY::HT || phy_ == ::fuchsia::wlan::mlme::PHY::VHT);
    }

    // SanitizeChannel tests the validation of input wlan_channel_t
    // to support interoperable Join and Association.
    // This provides a defensive meature to inconsistent
    // announcement from the neighbor BSS, and potential ignorance of SME.
    static wlan_channel_t SanitizeChannel(const wlan_channel_t& chan);

   private:
    ::fuchsia::wlan::mlme::BSSDescription bss_;
    wlan_channel_t channel_;
    wlan_channel_t bss_channel_;
    ::fuchsia::wlan::mlme::PHY phy_;
    common::MacAddr bssid_;
};

}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_CLIENT_JOIN_CONTEXT_H_
