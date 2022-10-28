// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is in C++, as it interfaces with C++-only libraries.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/ieee80211.h"

#include <fidl/fuchsia.wlan.common/cpp/wire_types.h>

#include <wlan/common/channel.h>
#include <wlan/common/ieee80211.h>

size_t ieee80211_get_header_len(const struct ieee80211_frame_header* fw) {
  return ieee80211_hdrlen(fw);
}

struct ieee80211_hw* ieee80211_alloc_hw(size_t priv_data_len, const struct ieee80211_ops* ops) {
  return nullptr;
}

bool ieee80211_is_valid_chan(uint8_t primary) {
  fuchsia_wlan_common::wire::WlanChannel chan = {
      .primary = primary,
      .cbw = fuchsia_wlan_common::wire::ChannelBandwidth::kCbw20,
      .secondary80 = 0,
  };

  return wlan::common::IsValidChan(chan);
}

uint16_t ieee80211_get_center_freq(uint8_t ch_num) {
  fuchsia_wlan_common::wire::WlanChannel chan = {
      .primary = ch_num,
      .cbw = fuchsia_wlan_common::wire::ChannelBandwidth::kCbw20,
      .secondary80 = 0,
  };

  return wlan::common::GetCenterFreq(chan);
}

bool ieee80211_has_protected(const struct ieee80211_frame_header* fh) {
  return ieee80211_pkt_is_protected(fh);
}

bool ieee80211_is_data(const struct ieee80211_frame_header* fh) {
  return ieee80211_get_frame_type(fh) == IEEE80211_FRAME_TYPE_DATA;
}

bool ieee80211_is_data_present(const struct ieee80211_frame_header* fh) {
  return ieee80211_is_data(fh) &&
         ((static_cast<uint8_t>(ieee80211_get_frame_subtype(fh)) & 0x40) == 0);
}

bool ieee80211_is_data_qos(const struct ieee80211_frame_header* fh) {
  return ieee80211_is_qos_data(fh);
}

uint8_t ieee80211_get_tid(const struct ieee80211_frame_header* fh) {
  const uint8_t* qos_ctl = reinterpret_cast<const uint8_t*>(fh) + ieee80211_get_qos_ctrl_offset(fh);
  return qos_ctl[0] & 0xF;
}

bool ieee80211_is_back_req(const struct ieee80211_frame_header* fh) {
  return (ieee80211_get_frame_type(fh) == IEEE80211_FRAME_TYPE_CTRL) &&
         (ieee80211_get_frame_subtype(fh) == IEEE80211_FRAME_SUBTYPE_BACK_REQ);
}
