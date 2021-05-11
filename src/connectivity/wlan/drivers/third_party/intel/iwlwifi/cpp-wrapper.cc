// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/cpp-wrapper.h"

#include <wlan/common/channel.h>

using wlan::common::GetCenterFreq;
using wlan::common::IsValidChan;

bool is_valid_chan(uint8_t primary) {
  wlan_channel_t chan = {
      .primary = primary,
      .cbw = WLAN_CHANNEL_BANDWIDTH__20,
      .secondary80 = 0,
  };

  return IsValidChan(chan);
}

uint16_t get_center_freq(uint8_t ch_num) {
  wlan_channel_t chan = {
      .primary = ch_num,
      .cbw = WLAN_CHANNEL_BANDWIDTH__20,
      .secondary80 = 0,
  };

  return GetCenterFreq(chan);
}
