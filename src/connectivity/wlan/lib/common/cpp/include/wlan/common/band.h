// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_BAND_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_BAND_H_

#include <fuchsia/hardware/wlan/associnfo/c/banjo.h>
#include <fuchsia/hardware/wlan/phyinfo/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>

#include <cstdint>
#include <string>

namespace wlan {
namespace common {

wlan_band_t GetWlanBand(const wlan_channel_t& channel);
std::string WlanBandStr(wlan_band_t band);
std::string WlanBandStr(const wlan_channel_t& channel);
zx_status_t ToFidl(::fuchsia::wlan::common::WlanBand* out_fidl_band, wlan_band_t banjo_band);
wlan_band_t FromFidl(::fuchsia::wlan::common::WlanBand fidl_band);

}  // namespace common
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_BAND_H_
