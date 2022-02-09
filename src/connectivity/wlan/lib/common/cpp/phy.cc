// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>

#include <cstdio>

#include <wlan/common/phy.h>

namespace wlan {
namespace common {

namespace wlan_common = ::fuchsia::wlan::common;

wlan_phy_type_t FromFidl(const wlan_common::WlanPhyType& fidl_phy) {
  return static_cast<wlan_phy_type_t>(fidl_phy);
}

zx_status_t ToFidl(wlan_common::WlanPhyType* out_fidl_phy, const wlan_phy_type_t banjo_phy) {
  switch (banjo_phy) {
    case WLAN_PHY_TYPE_DSSS:
    case WLAN_PHY_TYPE_HR:
    case WLAN_PHY_TYPE_OFDM:
    case WLAN_PHY_TYPE_ERP:
    case WLAN_PHY_TYPE_HT:
    case WLAN_PHY_TYPE_DMG:
    case WLAN_PHY_TYPE_VHT:
    case WLAN_PHY_TYPE_TVHT:
    case WLAN_PHY_TYPE_S1G:
    case WLAN_PHY_TYPE_CDMG:
    case WLAN_PHY_TYPE_CMMG:
    case WLAN_PHY_TYPE_HE:
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  *out_fidl_phy = static_cast<wlan_common::WlanPhyType>(banjo_phy);
  return ZX_OK;
}

std::string Alpha2ToStr(cpp20::span<const uint8_t> alpha2) {
  if (alpha2.size() != WLANPHY_ALPHA2_LEN) {
    return "Invalid alpha2 length";
  }
  char buf[WLANPHY_ALPHA2_LEN * 8 + 1];
  auto data = alpha2.data();
  bool is_printable = std::isprint(data[0]) && std::isprint(data[1]);
  if (is_printable) {
    snprintf(buf, sizeof(buf), "%c%c", data[0], data[1]);
  } else {
    snprintf(buf, sizeof(buf), "(%u)(%u)", data[0], data[1]);
  }
  return std::string(buf);
}

}  // namespace common
}  // namespace wlan
