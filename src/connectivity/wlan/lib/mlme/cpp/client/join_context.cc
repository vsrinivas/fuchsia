// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/channel.h>
#include <wlan/mlme/client/join_context.h>

namespace wlan {
JoinContext::JoinContext(::fuchsia::wlan::mlme::BSSDescription bss,
                         ::fuchsia::wlan::common::PHY phy,
                         ::fuchsia::wlan::common::CBW cbw)
    : bss_(std::move(bss)) {
  bssid_ = common::MacAddr(bss_.bssid);
  bss_channel_ = wlan_channel_t{
      .primary = bss_.chan.primary,
      .cbw = static_cast<uint8_t>(bss_.chan.cbw),
  };

  // Discern join configuration from BSS announcement
  // Note primary channel can't be different.
  phy_ = common::FromFidl(phy);
  channel_ = bss_channel_;
  channel_.cbw = static_cast<uint8_t>(cbw);
  channel_ = SanitizeChannel(channel_);
}

wlan_channel_t JoinContext::SanitizeChannel(const wlan_channel_t& chan) {
  if (common::IsValidChan(chan)) {
    return chan;
  }

  wlan_channel_t chan_fallback = chan;
  chan_fallback.cbw = CBW20;
  errorf("Sanitize the invalid channel: %s to %s\n",
         common::ChanStrLong(chan).c_str(),
         common::ChanStrLong(chan_fallback).c_str());
  return chan_fallback;
}

}  // namespace wlan
