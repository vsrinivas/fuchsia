// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_TESTING_WLANTAP_DRIVER_UTILS_H_
#define SRC_CONNECTIVITY_WLAN_TESTING_WLANTAP_DRIVER_UTILS_H_

#include <fidl/fuchsia.wlan.softmac/cpp/driver/wire.h>
#include <fidl/fuchsia.wlan.tap/cpp/wire.h>
#include <fuchsia/hardware/wlan/softmac/c/banjo.h>
#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>
#include <fuchsia/wlan/device/cpp/fidl.h>

namespace wlan_common = fuchsia_wlan_common::wire;
namespace wlan_device = fuchsia_wlan_device::wire;
namespace wlan_tap = fuchsia_wlan_tap::wire;
namespace wlan_softmac = fuchsia_wlan_softmac::wire;

namespace wlan {

// Functions for converting between FIDL classes and related wlan C structs
wlan_common::WlanMacRole ConvertMacRole(uint16_t role);
uint32_t ConvertCaps(const ::std::vector<wlan_device::Capability>& caps);
zx_status_t ConvertBandInfoToCapability(const wlan_device::BandInfo& in,
                                        wlan_softmac_band_capability_t* out);
void ConvertTapPhyConfig(wlan_softmac::WlanSoftmacInfo* mac_info,
                         const wlan_tap::WlantapPhyConfig& tap_phy_config, fidl::AnyArena& arena);
zx_status_t ConvertTapPhyConfig(
    wlan_mac_role_t supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES],
    uint8_t* supported_mac_roles_count, const wlan_tap::WlantapPhyConfig& tap_phy_config);
wlan_tx_status_t ConvertTxStatus(const wlan_common::WlanTxStatus& in);
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_TESTING_WLANTAP_DRIVER_UTILS_H_
