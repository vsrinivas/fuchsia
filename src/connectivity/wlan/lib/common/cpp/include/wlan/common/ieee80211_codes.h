// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_IEEE80211_CODES_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_IEEE80211_CODES_H_

#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <stdint.h>

namespace wlan {
namespace common {

// Convert a ReasonCode value to/from a uint16_t value.  Unknown values are
// defaulted to REASON_UNSPECIFIED.
// IEEE Std 802.11-2016, 9.4.1.7, Table 9-45
uint16_t ConvertReasonCode(::fuchsia::wlan::ieee80211::ReasonCode reason_code);
::fuchsia::wlan::ieee80211::ReasonCode ConvertReasonCode(uint16_t reason_code);

// Convert a StatusCode value to/from a uint16_t value.  Unknown values are
// defaulted to REFUSED_REASON_UNSPECIFIED.
// IEEE Std 802.11-2016, 9.4.1.9, Table 9-46
uint16_t ConvertStatusCode(::fuchsia::wlan::ieee80211::StatusCode status);
::fuchsia::wlan::ieee80211::StatusCode ConvertStatusCode(uint16_t status);

}  // namespace common
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_IEEE80211_CODES_H_
