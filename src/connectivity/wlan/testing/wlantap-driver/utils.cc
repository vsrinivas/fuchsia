// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <fuchsia/hardware/ethernet/c/banjo.h>
#include <fuchsia/hardware/wlan/associnfo/c/banjo.h>
#include <fuchsia/hardware/wlan/softmac/c/banjo.h>
#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>
#include <fuchsia/wlan/device/cpp/fidl.h>
#include <fuchsia/wlan/internal/cpp/fidl.h>
#include <lib/ddk/debug.h>

#include <wlan/common/band.h>
#include <wlan/common/channel.h>
#include <wlan/common/element.h>
#include <wlan/common/parse_element.h>
#include <wlan/common/phy.h>

namespace wlan {

namespace wlan_common = fuchsia_wlan_common::wire;
namespace wlan_device = fuchsia_wlan_device::wire;
namespace wlan_internal = fuchsia_wlan_internal::wire;
namespace wlan_tap = fuchsia_wlan_tap::wire;

void FillSupportedPhys(
    wlan_phy_type_t out_supported_phys_list[fuchsia_wlan_common_MAX_SUPPORTED_PHY_TYPES],
    uint8_t* out_supported_phys_count, const fidl::VectorView<wlan_common::WlanPhyType>& phys) {
  *out_supported_phys_count = 0;
  for (auto sp : phys) {
    out_supported_phys_list[*out_supported_phys_count] = static_cast<wlan_phy_type_t>(sp);
    ++*out_supported_phys_count;
  }
}

zx_status_t ConvertBandInfoToCapability(const wlan_device::BandInfo& in,
                                        wlan_softmac_band_capability_t* out) {
  memset(out, 0, sizeof(*out));
  switch (in.band) {
    case WLAN_BAND_TWO_GHZ:
      out->band = wlan_common::WlanBand::kTwoGhz;
      break;
    case WLAN_BAND_FIVE_GHZ:
      out->band = wlan_common::WlanBand::kFiveGhz;
      break;
    default:
      zxlogf(ERROR, "Invalid band: %u", static_cast<uint8_t>(in.band));
      return ZX_ERR_INVALID_ARGS;
  }

  if (in.ht_caps != nullptr) {
    out->ht_supported = true;
    out->ht_caps = ::wlan::common::ParseHtCapabilities(in.ht_caps->bytes)->ToDdk();
  } else {
    out->ht_supported = false;
  }

  if (in.vht_caps != nullptr) {
    out->vht_supported = true;
    out->vht_caps = ::wlan::common::ParseVhtCapabilities(in.vht_caps->bytes)->ToDdk();
  } else {
    out->vht_supported = false;
  }

  out->basic_rate_count =
      std::min<size_t>(in.rates.count(), wlan_internal::kMaxSupportedBasicRates);
  std::copy_n(in.rates.data(), out->basic_rate_count, out->basic_rate_list);

  out->operating_channel_count = std::min<size_t>(
      in.operating_channels.count(), fuchsia_wlan_ieee80211_MAX_UNIQUE_CHANNEL_NUMBERS);
  std::copy_n(in.operating_channels.data(), out->operating_channel_count,
              out->operating_channel_list);
  return ZX_OK;
}

void ConvertTapPhyConfig(wlan_softmac::WlanSoftmacInfo* mac_info,
                         const wlan_tap::WlantapPhyConfig& tap_phy_config, fidl::AnyArena& arena) {
  auto builder = wlan_softmac::WlanSoftmacInfo::Builder(arena);

  builder.sta_addr(tap_phy_config.sta_addr);
  builder.mac_role(tap_phy_config.mac_role);
  builder.supported_phys(tap_phy_config.supported_phys);
  builder.hardware_capability(tap_phy_config.hardware_capability);

  size_t band_cap_count =
      std::min(tap_phy_config.bands.count(), static_cast<size_t>(wlan_common::kMaxBands));
  wlan_softmac::WlanSoftmacBandCapability band_cap_list[wlan_common::kMaxBands];
  // FIDL type conversion from WlantapPhyConfig to WlanSoftmacBandCapability.
  for (size_t i = 0; i < band_cap_count; i++) {
    band_cap_list[i].band = (tap_phy_config.bands)[i].band;

    if ((tap_phy_config.bands)[i].ht_caps != nullptr) {
      band_cap_list[i].ht_supported = true;
      band_cap_list[i].ht_caps.bytes = (tap_phy_config.bands)[i].ht_caps->bytes;
    } else {
      band_cap_list[i].ht_supported = false;
    }

    if ((tap_phy_config.bands)[i].vht_caps != nullptr) {
      band_cap_list[i].vht_supported = true;
      band_cap_list[i].vht_caps.bytes = tap_phy_config.bands[i].vht_caps->bytes;
    } else {
      band_cap_list[i].vht_supported = false;
    }

    band_cap_list[i].basic_rate_count =
        std::min<size_t>((tap_phy_config.bands)[i].rates.count(),
                         fuchsia_wlan_internal::wire::kMaxSupportedBasicRates);
    std::copy_n((tap_phy_config.bands)[i].rates.data(), band_cap_list[i].basic_rate_count,
                band_cap_list[i].basic_rate_list.begin());

    band_cap_list[i].operating_channel_count =
        std::min<size_t>((tap_phy_config.bands)[i].operating_channels.count(),
                         fuchsia_wlan_ieee80211_MAX_UNIQUE_CHANNEL_NUMBERS);
    std::copy_n((tap_phy_config.bands)[i].operating_channels.data(),
                band_cap_list[i].operating_channel_count,
                band_cap_list[i].operating_channel_list.begin());
  }

  auto band_cap_vec = std::vector<fuchsia_wlan_softmac::wire::WlanSoftmacBandCapability>(
      band_cap_list, band_cap_list + band_cap_count);
  builder.band_caps(
      fidl::VectorView<fuchsia_wlan_softmac::wire::WlanSoftmacBandCapability>(arena, band_cap_vec));

  *mac_info = builder.Build();
}

zx_status_t ConvertTapPhyConfig(
    wlan_mac_role_t supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES],
    uint8_t* supported_mac_roles_count, const wlan_tap::WlantapPhyConfig& tap_phy_config) {
  switch (tap_phy_config.mac_role) {
    case wlan_common::WlanMacRole::kClient:
      supported_mac_roles_list[0] = WLAN_MAC_ROLE_CLIENT;
      break;
    case wlan_common::WlanMacRole::kAp:
      supported_mac_roles_list[0] = WLAN_MAC_ROLE_AP;
      break;
    case wlan_common::WlanMacRole::kMesh:
      supported_mac_roles_list[0] = WLAN_MAC_ROLE_MESH;
      break;
    default:
      zxlogf(ERROR, "WlantapPhyConfig contains unsupported WlanMacRole: %u",
             static_cast<uint8_t>(tap_phy_config.mac_role));
      return ZX_ERR_NOT_SUPPORTED;
  }
  *supported_mac_roles_count = 1;
  return ZX_OK;
}

wlan_tx_status_t ConvertTxStatus(const wlan_common::WlanTxStatus& in) {
  wlan_tx_status_t out;
  std::copy(in.peer_addr.cbegin(), in.peer_addr.cend(), out.peer_addr);
  for (size_t i = 0; i < in.tx_status_entry.size(); ++i) {
    out.tx_status_entry[i].tx_vector_idx = in.tx_status_entry[i].tx_vector_idx;
    out.tx_status_entry[i].attempts = in.tx_status_entry[i].attempts;
  }
  if (in.result == wlan_common::WlanTxResult::kSuccess) {
    out.result = WLAN_TX_RESULT_SUCCESS;
  } else {
    out.result = WLAN_TX_RESULT_FAILED;
  }
  return out;
}
}  // namespace wlan
