// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_PHY_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_PHY_H_

#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>
#include <lib/stdcompat/span.h>

#include <array>
#include <string>

namespace wlan {
namespace common {

namespace wlan_common = ::fuchsia::wlan::common;

wlan_phy_type_t FromFidl(const ::fuchsia::wlan::common::WlanPhyType& fidl_phy);
zx_status_t ToFidl(::fuchsia::wlan::common::WlanPhyType* out_fidl_phy,
                   const wlan_phy_type_t banjo_phy);
std::string Alpha2ToStr(cpp20::span<const uint8_t> alpha2);

}  // namespace common
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_PHY_H_
