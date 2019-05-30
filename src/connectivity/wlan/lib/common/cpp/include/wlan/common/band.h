// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_BAND_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_BAND_H_

#include <ddk/hw/wlan/wlaninfo.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/protocol/info.h>

#include <cstdint>
#include <string>

namespace wlan {
namespace common {

wlan_info_band_t GetBand(const wlan_channel_t& chan);
std::string BandStr(uint8_t band);
std::string BandStr(wlan_info_band_t band);
std::string BandStr(const wlan_channel_t& chan);
::fuchsia::wlan::common::Band BandToFidl(uint8_t band);
::fuchsia::wlan::common::Band BandToFidl(wlan_info_band_t band);
wlan_info_band_t BandFromFidl(::fuchsia::wlan::common::Band band);

}  // namespace common
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_BAND_H_
