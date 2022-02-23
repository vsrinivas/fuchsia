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

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_device = ::fuchsia::wlan::device;
namespace wlan_internal = ::fuchsia::wlan::internal;
namespace wlan_tap = ::fuchsia::wlan::tap;

void FillSupportedPhys(
    wlan_phy_type_t out_supported_phys_list[fuchsia_wlan_common_MAX_SUPPORTED_PHY_TYPES],
    uint8_t* out_supported_phys_count, const ::std::vector<wlan_common::WlanPhyType>& phys) {
  *out_supported_phys_count = 0;
  for (auto sp : phys) {
    out_supported_phys_list[*out_supported_phys_count] = common::FromFidl(sp);
    ++*out_supported_phys_count;
  }
}

uint32_t ConvertDriverFeatures(const ::std::vector<wlan_common::DriverFeature>& dfs) {
  uint32_t ret = 0;
  for (auto df : dfs) {
    switch (df) {
      case wlan_common::DriverFeature::SCAN_OFFLOAD:
        ret |= WLAN_INFO_DRIVER_FEATURE_SCAN_OFFLOAD;
        break;
      case wlan_common::DriverFeature::RATE_SELECTION:
        ret |= WLAN_INFO_DRIVER_FEATURE_RATE_SELECTION;
        break;
      case wlan_common::DriverFeature::SYNTH:
        ret |= WLAN_INFO_DRIVER_FEATURE_SYNTH;
        break;
      case wlan_common::DriverFeature::TX_STATUS_REPORT:
        ret |= WLAN_INFO_DRIVER_FEATURE_TX_STATUS_REPORT;
        break;
      case wlan_common::DriverFeature::DFS:
        ret |= WLAN_INFO_DRIVER_FEATURE_DFS;
        break;
      case wlan_common::DriverFeature::PROBE_RESP_OFFLOAD:
        ret |= WLAN_INFO_DRIVER_FEATURE_PROBE_RESP_OFFLOAD;
        break;
      case wlan_common::DriverFeature::SAE_SME_AUTH:
        ret |= WLAN_INFO_DRIVER_FEATURE_SAE_SME_AUTH;
        break;
      case wlan_common::DriverFeature::SAE_DRIVER_AUTH:
        ret |= WLAN_INFO_DRIVER_FEATURE_SAE_DRIVER_AUTH;
        break;
      case wlan_common::DriverFeature::MFP:
        ret |= WLAN_INFO_DRIVER_FEATURE_MFP;
        break;
        // TODO(fxbug.dev/41640): Remove this flag once FullMAC drivers stop interacting with SME.
      case wlan_common::DriverFeature::TEMP_SOFTMAC:
        // Vendor driver has no control over this flag.
        break;
    }
  }
  return ret;
}

wlan_mac_role_t ConvertMacRole(wlan_common::WlanMacRole role) {
  switch (role) {
    case wlan_common::WlanMacRole::AP:
      return WLAN_MAC_ROLE_AP;
    case wlan_common::WlanMacRole::CLIENT:
      return WLAN_MAC_ROLE_CLIENT;
    case wlan_common::WlanMacRole::MESH:
      return WLAN_MAC_ROLE_MESH;
  }
}

wlan_common::WlanMacRole ConvertMacRole(uint16_t role) {
  switch (role) {
    case WLAN_MAC_ROLE_AP:
      return wlan_common::WlanMacRole::AP;
    case WLAN_MAC_ROLE_CLIENT:
      return wlan_common::WlanMacRole::CLIENT;
    case WLAN_MAC_ROLE_MESH:
      return wlan_common::WlanMacRole::MESH;
  }
  ZX_ASSERT(0);
}

void ConvertBandInfoToCapability(const wlan_device::BandInfo& in,
                                 wlan_softmac_band_capability_t* out) {
  memset(out, 0, sizeof(*out));
  out->band = wlan::common::FromFidl(in.band);

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
      std::min<size_t>(in.rates.size(), wlan_internal::MAX_SUPPORTED_BASIC_RATES);
  std::copy_n(in.rates.data(), out->basic_rate_count, out->basic_rate_list);

  out->operating_channel_count = std::min<size_t>(
      in.operating_channels.size(), fuchsia_wlan_ieee80211_MAX_UNIQUE_CHANNEL_NUMBERS);
  std::copy_n(in.operating_channels.data(), out->operating_channel_count,
              out->operating_channel_list);
}

zx_status_t ConvertTapPhyConfig(wlan_softmac_info_t* mac_info,
                                const wlan_tap::WlantapPhyConfig& tap_phy_config) {
  std::memset(mac_info, 0, sizeof(*mac_info));
  std::copy_n(tap_phy_config.sta_addr.begin(), ETH_MAC_SIZE, mac_info->sta_addr);

  FillSupportedPhys(mac_info->supported_phys_list, &mac_info->supported_phys_count,
                    tap_phy_config.supported_phys);
  mac_info->driver_features = ConvertDriverFeatures(tap_phy_config.driver_features);
  mac_info->mac_role = ConvertMacRole(tap_phy_config.mac_role);
  mac_info->hardware_capability = tap_phy_config.hardware_capability;
  mac_info->band_cap_count =
      std::min(tap_phy_config.bands.size(), static_cast<size_t>(fuchsia_wlan_common_MAX_BANDS));

  for (size_t i = 0; i < mac_info->band_cap_count; ++i) {
    ConvertBandInfoToCapability((tap_phy_config.bands)[i], &mac_info->band_cap_list[i]);
  }
  return ZX_OK;
}

zx_status_t ConvertTapPhyConfig(
    wlan_mac_role_t supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES],
    uint8_t* supported_mac_roles_count, const wlan_tap::WlantapPhyConfig& tap_phy_config) {
  switch (tap_phy_config.mac_role) {
    case wlan_common::WlanMacRole::CLIENT:
      supported_mac_roles_list[0] = WLAN_MAC_ROLE_CLIENT;
      break;
    case wlan_common::WlanMacRole::AP:
      supported_mac_roles_list[0] = WLAN_MAC_ROLE_AP;
      break;
    case wlan_common::WlanMacRole::MESH:
      supported_mac_roles_list[0] = WLAN_MAC_ROLE_MESH;
      break;
    default:
      zxlogf(ERROR, "MAC role %u not supported", tap_phy_config.mac_role);
      return ZX_ERR_NOT_SUPPORTED;
  }
  *supported_mac_roles_count = 1;
  return ZX_OK;
}

discovery_support_t ConvertDiscoverySupport(const wlan_common::DiscoverySupport& in) {
  discovery_support_t support;
  support.scan_offload.supported = in.scan_offload.supported;
  support.probe_response_offload.supported = in.probe_response_offload.supported;
  return support;
}

mac_sublayer_support_t ConvertMacSublayerSupport(const wlan_common::MacSublayerSupport& in) {
  mac_sublayer_support_t support;
  support.rate_selection_offload.supported = in.rate_selection_offload.supported;
  support.device.is_synthetic = in.device.is_synthetic;
  switch (in.device.mac_implementation_type) {
    case wlan_common::MacImplementationType::SOFTMAC:
      support.device.mac_implementation_type = MAC_IMPLEMENTATION_TYPE_SOFTMAC;
      break;
    default:
      zxlogf(ERROR, "MAC implementation type %hhu not supported",
             in.device.mac_implementation_type);
      break;
  }
  support.device.tx_status_report_supported = in.device.tx_status_report_supported;
  switch (in.data_plane.data_plane_type) {
    case wlan_common::DataPlaneType::ETHERNET_DEVICE:
      support.data_plane.data_plane_type = DATA_PLANE_TYPE_ETHERNET_DEVICE;
      break;
    case wlan_common::DataPlaneType::GENERIC_NETWORK_DEVICE:
      support.data_plane.data_plane_type = DATA_PLANE_TYPE_GENERIC_NETWORK_DEVICE;
      break;
    default:
      zxlogf(ERROR, "Data plane type %hhu not supported", in.data_plane.data_plane_type);
      break;
  }
  return support;
}

security_support_t ConvertSecuritySupport(const wlan_common::SecuritySupport& in) {
  security_support_t support;
  support.mfp.supported = in.mfp.supported;
  support.sae.supported = in.sae.supported;
  switch (in.sae.handler) {
    case wlan_common::SaeHandler::DRIVER:
      support.sae.handler = SAE_HANDLER_DRIVER;
      break;
    case wlan_common::SaeHandler::SME:
      support.sae.handler = SAE_HANDLER_SME;
      break;
    default:
      zxlogf(ERROR, "SAE handler %hhu not supported", in.sae.handler);
      break;
  }
  return support;
}

spectrum_management_support_t ConvertSpectrumManagementSupport(
    const wlan_common::SpectrumManagementSupport& in) {
  spectrum_management_support_t support;
  support.dfs.supported = in.dfs.supported;
  return support;
}

wlan_tx_status_t ConvertTxStatus(const wlan_common::WlanTxStatus& in) {
  wlan_tx_status_t out;
  std::copy(in.peer_addr.cbegin(), in.peer_addr.cend(), out.peer_addr);
  for (size_t i = 0; i < in.tx_status_entry.size(); ++i) {
    out.tx_status_entry[i].tx_vector_idx = in.tx_status_entry[i].tx_vector_idx;
    out.tx_status_entry[i].attempts = in.tx_status_entry[i].attempts;
  }
  if (in.result == wlan_common::WlanTxResult::SUCCESS) {
    out.result = WLAN_TX_RESULT_SUCCESS;
  } else {
    out.result = WLAN_TX_RESULT_FAILED;
  }
  return out;
}
}  // namespace wlan
