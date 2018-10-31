// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/span.h>
#include <wlan/protocol/info.h>
#include <cstddef>

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_DEVICE_CAPS_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_DEVICE_CAPS_H_
// Utilities for retrieving information from device capabilities

namespace wlan {

const wlan_band_info_t* FindBandByChannel(const wlan_info_t& device_info, uint8_t channel);

const Span<const uint8_t> GetRatesByChannel(const wlan_info_t& device_info, uint8_t channel);

}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_DEVICE_CAPS_H_
