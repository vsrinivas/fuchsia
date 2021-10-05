// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/wlan/mac/c/banjo.h>
#include <lib/stdcompat/span.h>

#include <cstddef>

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_DEVICE_CAPS_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_DEVICE_CAPS_H_
// Utilities for retrieving information from device capabilities

namespace wlan {

const wlan_info_band_info_t* FindBandByChannel(const wlanmac_info_t& device_info, uint8_t channel);

const cpp20::span<const uint8_t> GetRatesByChannel(const wlanmac_info_t& device_info,
                                                   uint8_t channel);

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_DEVICE_CAPS_H_
