// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>

#include <wlan/mlme/device_caps.h>

namespace wlan {

const wlan_info_band_info_t* FindBandByChannel(const wlanmac_info_t& device_info, uint8_t channel) {
  for (size_t i = 0; i < device_info.bands_count; ++i) {
    for (auto& c : device_info.bands[i].supported_channels.channels) {
      if (c == channel) {
        return &device_info.bands[i];
      } else if (c == 0) {
        break;
      }
    }
  }
  return nullptr;
}

const fbl::Span<const uint8_t> GetRatesByChannel(const wlanmac_info_t& device_info,
                                                 uint8_t channel) {
  const wlan_info_band_info_t* band = FindBandByChannel(device_info, channel);
  if (band == nullptr) {
    return {};
  }

  size_t num_rates = strnlen(reinterpret_cast<const char*>(band->rates), sizeof(band->rates));
  return {band->rates, num_rates};
}

}  // namespace wlan
