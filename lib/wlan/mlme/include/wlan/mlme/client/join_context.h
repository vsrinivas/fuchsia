// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

namespace wlan {

class JoinContext {
   public:
    JoinContext(::fuchsia::wlan::mlme::BSSDescription bss, wlan_channel_t channel,
                ::fuchsia::wlan::mlme::PHY phy)
        : bss_(std::move(bss)), channel_(channel), phy_(phy) {
        bssid_ = common::MacAddr(bss_.bssid);
        bss_channel_ = wlan_channel_t{
            .primary = bss_.chan.primary,
            .cbw = static_cast<uint8_t>(bss_.chan.cbw),
        };
    }

    const common::MacAddr& bssid() const { return bssid_; }

    const wlan_channel_t& channel() const { return channel_; }

    const wlan_channel_t& bss_channel() const { return bss_channel_; }

    const ::fuchsia::wlan::mlme::BSSDescription* bss() const { return &bss_; }

    bool IsHtOrLater() const {
        return (phy_ == ::fuchsia::wlan::mlme::PHY::HT || phy_ == ::fuchsia::wlan::mlme::PHY::VHT);
    }

   private:
    ::fuchsia::wlan::mlme::BSSDescription bss_;
    wlan_channel_t channel_;
    wlan_channel_t bss_channel_;
    ::fuchsia::wlan::mlme::PHY phy_;
    common::MacAddr bssid_;
};

}  // namespace wlan
