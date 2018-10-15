// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/protocol/info.h>
#include <cstddef>

// Utilities for retrieving information from device capabilities

namespace wlan {

const wlan_band_info_t* FindBandByChannel(const wlan_info_t& device_info, uint8_t channel);

const uint8_t* GetRatesByChannel(const wlan_info_t& device_info,
                                 uint8_t channel,
                                 size_t* num_rates);

} // namespace wlan
