// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is in C++, as it interfaces with C++-only libraries.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/ieee80211.h"

#include <wlan/common/channel.h>
#include <wlan/protocol/ieee80211.h>

size_t ieee80211_get_header_len(const struct ieee80211_frame_header* fw) {
  return ieee80211_hdrlen(fw);
}

struct ieee80211_hw* ieee80211_alloc_hw(size_t priv_data_len, const struct ieee80211_ops* ops) {
  return nullptr;
}

bool ieee80211_is_valid_chan(uint8_t primary) {
  wlan_channel_t chan = {
      .primary = primary,
      .cbw = CHANNEL_BANDWIDTH_CBW20,
      .secondary80 = 0,
  };

  return wlan::common::IsValidChan(chan);
}

uint16_t ieee80211_get_center_freq(uint8_t ch_num) {
  wlan_channel_t chan = {
      .primary = ch_num,
      .cbw = CHANNEL_BANDWIDTH_CBW20,
      .secondary80 = 0,
  };

  return wlan::common::GetCenterFreq(chan);
}
