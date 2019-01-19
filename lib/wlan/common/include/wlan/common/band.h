// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_COMMON_INCLUDE_WLAN_COMMON_BAND_H_
#define GARNET_LIB_WLAN_COMMON_INCLUDE_WLAN_COMMON_BAND_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <wlan/protocol/mac.h>

#include <cstdint>
#include <string>

namespace wlan {
namespace common {

Band GetBand(const wlan_channel_t& chan);
std::string BandStr(uint8_t band);
std::string BandStr(Band band);
std::string BandStr(const wlan_channel_t& chan);
::fuchsia::wlan::common::Band BandToFidl(uint8_t band);
::fuchsia::wlan::common::Band BandToFidl(Band band);
Band BandFromFidl(::fuchsia::wlan::common::Band band);

}  // namespace common
}  // namespace wlan

#endif  // GARNET_LIB_WLAN_COMMON_INCLUDE_WLAN_COMMON_BAND_H_
