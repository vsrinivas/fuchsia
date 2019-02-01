// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/device_caps.h>
#include <cstring>

namespace wlan {

const wlan_band_info_t* FindBandByChannel(const wlan_info_t& device_info, uint8_t channel) {
    for (size_t i = 0; i < device_info.num_bands; ++i) {
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

const Span<const uint8_t> GetRatesByChannel(const wlan_info_t& device_info, uint8_t channel) {
    const wlan_band_info_t* band = FindBandByChannel(device_info, channel);
    if (band == nullptr) { return {}; }

    size_t num_rates =
        strnlen(reinterpret_cast<const char*>(band->basic_rates), sizeof(band->basic_rates));
    return {band->basic_rates, num_rates};
}

}  // namespace wlan
