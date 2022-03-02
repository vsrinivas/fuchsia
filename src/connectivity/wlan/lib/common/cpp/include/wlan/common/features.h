// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_FEATURES_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_FEATURES_H_

#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>

namespace wlan::common {

// Convert discovery support DDK type to corresponding FIDL type.
zx_status_t ConvertDiscoverySupportToFidl(const discovery_support_t& in,
                                          ::fuchsia::wlan::common::DiscoverySupport* out);

// Convert discovery support FIDL type to corresponding DDK type.
zx_status_t ConvertDiscoverySupportToDdk(const ::fuchsia::wlan::common::DiscoverySupport& in,
                                         discovery_support_t* out);

// Convert MAC sublayer support DDK type to corresponding FIDL type.
zx_status_t ConvertMacSublayerSupportToFidl(const mac_sublayer_support_t& in,
                                            ::fuchsia::wlan::common::MacSublayerSupport* out);

// Convert MAC sublayer support FIDL type to corresponding DDK type.
zx_status_t ConvertMacSublayerSupportToDdk(const ::fuchsia::wlan::common::MacSublayerSupport& in,
                                           mac_sublayer_support_t* out);

// Convert security support DDK type to corresponding FIDL type.
zx_status_t ConvertSecuritySupportToFidl(const security_support_t& in,
                                         ::fuchsia::wlan::common::SecuritySupport* out);

// Convert security support FIDL type to corresponding DDK type.
zx_status_t ConvertSecuritySupportToDdk(const ::fuchsia::wlan::common::SecuritySupport& in,
                                        security_support_t* out);

// Convert spectrum management support DDK type to corresponding FIDL type.
zx_status_t ConvertSpectrumManagementSupportToFidl(
    const spectrum_management_support_t& in,
    ::fuchsia::wlan::common::SpectrumManagementSupport* out);

// Convert spectrum management support FIDL type to corresponding DDK type.
zx_status_t ConvertSpectrumManagementSupportToDdk(
    const ::fuchsia::wlan::common::SpectrumManagementSupport& in,
    spectrum_management_support_t* out);

}  // namespace wlan::common

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_FEATURES_H_
