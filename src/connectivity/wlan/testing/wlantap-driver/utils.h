// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_TESTING_WLANTAP_DRIVER_UTILS_H_
#define SRC_CONNECTIVITY_WLAN_TESTING_WLANTAP_DRIVER_UTILS_H_

#include <fuchsia/wlan/device/cpp/fidl.h>
#include <fuchsia/wlan/tap/cpp/fidl.h>

#include <ddk/hw/wlan/wlaninfo.h>
#include <ddk/protocol/wlan/mac.h>
#include <ddk/protocol/wlanphyimpl.h>

#include "utils.h"

namespace wlan {

// Functions for converting between FIDL classes and related wlan C structs
uint16_t ConvertSupportedPhys(const ::std::vector<::fuchsia::wlan::device::SupportedPhy>& phys);
uint32_t ConvertDriverFeatures(const ::std::vector<::fuchsia::wlan::common::DriverFeature>& dfs);
uint16_t ConvertMacRole(::fuchsia::wlan::device::MacRole role);
::fuchsia::wlan::device::MacRole ConvertMacRole(uint16_t role);
uint16_t ConvertMacRoles(::std::vector<::fuchsia::wlan::device::MacRole>& role);
uint32_t ConvertCaps(const ::std::vector<::fuchsia::wlan::device::Capability>& caps);
void ConvertBandInfo(const ::fuchsia::wlan::device::BandInfo& in, wlan_info_band_info_t* out);
zx_status_t ConvertTapPhyConfig(wlanmac_info_t* mac_info,
                                const ::fuchsia::wlan::tap::WlantapPhyConfig& tap_phy_config);
zx_status_t ConvertTapPhyConfig(wlanphy_impl_info_t* phy_impl_info,
                                const ::fuchsia::wlan::tap::WlantapPhyConfig& tap_phy_config);
wlan_tx_status_t ConvertTxStatus(const ::fuchsia::wlan::tap::WlanTxStatus& in);
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_TESTING_WLANTAP_DRIVER_UTILS_H_
