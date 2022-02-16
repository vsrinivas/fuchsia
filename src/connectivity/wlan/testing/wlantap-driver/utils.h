// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_TESTING_WLANTAP_DRIVER_UTILS_H_
#define SRC_CONNECTIVITY_WLAN_TESTING_WLANTAP_DRIVER_UTILS_H_

#include <fuchsia/hardware/wlan/phyinfo/c/banjo.h>
#include <fuchsia/hardware/wlan/softmac/c/banjo.h>
#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>
#include <fuchsia/wlan/device/cpp/fidl.h>
#include <fuchsia/wlan/tap/cpp/fidl.h>

namespace wlan {

// Functions for converting between FIDL classes and related wlan C structs
uint32_t ConvertDriverFeatures(const ::std::vector<::fuchsia::wlan::common::DriverFeature>& dfs);
wlan_mac_role_t ConvertMacRole(::fuchsia::wlan::common::WlanMacRole role);
::fuchsia::wlan::common::WlanMacRole ConvertMacRole(uint16_t role);
uint32_t ConvertCaps(const ::std::vector<::fuchsia::wlan::device::Capability>& caps);
void ConvertBandInfoToCapability(const ::fuchsia::wlan::device::BandInfo& in,
                                 wlan_softmac_band_capability_t* out);
zx_status_t ConvertTapPhyConfig(wlan_softmac_info_t* mac_info,
                                const ::fuchsia::wlan::tap::WlantapPhyConfig& tap_phy_config);
zx_status_t ConvertTapPhyConfig(
    wlan_mac_role_t supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES],
    uint8_t* supported_mac_roles_count,
    const ::fuchsia::wlan::tap::WlantapPhyConfig& tap_phy_config);
discovery_support_t ConvertDiscoverySupport(const ::fuchsia::wlan::common::DiscoverySupport& in);
mac_sublayer_support_t ConvertMacSublayerSupport(
    const ::fuchsia::wlan::common::MacSublayerSupport& in);
security_support_t ConvertSecuritySupport(const ::fuchsia::wlan::common::SecuritySupport& in);
spectrum_management_support_t ConvertSpectrumManagementSupport(
    const ::fuchsia::wlan::common::SpectrumManagementSupport& in);
wlan_tx_status_t ConvertTxStatus(const ::fuchsia::wlan::common::WlanTxStatus& in);
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_TESTING_WLANTAP_DRIVER_UTILS_H_
