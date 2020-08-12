// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <fuchsia/wlan/device/cpp/fidl.h>

#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/wlan/info.h>
#include <wlan/common/band.h>
#include <wlan/common/channel.h>
#include <wlan/common/element.h>
#include <wlan/common/parse_element.h>

namespace wlan {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_device = ::fuchsia::wlan::device;
namespace wlan_tap = ::fuchsia::wlan::tap;

uint16_t ConvertSupportedPhys(const ::std::vector<wlan_device::SupportedPhy>& phys) {
  uint16_t ret = 0;
  for (auto sp : phys) {
    switch (sp) {
      case wlan_device::SupportedPhy::DSSS:
        ret |= WLAN_INFO_PHY_TYPE_DSSS;
        break;
      case wlan_device::SupportedPhy::CCK:
        ret |= WLAN_INFO_PHY_TYPE_CCK;
        break;
      case wlan_device::SupportedPhy::OFDM:
        ret |= WLAN_INFO_PHY_TYPE_OFDM;
        break;
      case wlan_device::SupportedPhy::HT:
        ret |= WLAN_INFO_PHY_TYPE_HT;
        break;
      case wlan_device::SupportedPhy::VHT:
        ret |= WLAN_INFO_PHY_TYPE_VHT;
        break;
    }
  }
  return ret;
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

uint16_t ConvertMacRole(wlan_device::MacRole role) {
  switch (role) {
    case wlan_device::MacRole::AP:
      return WLAN_INFO_MAC_ROLE_AP;
    case wlan_device::MacRole::CLIENT:
      return WLAN_INFO_MAC_ROLE_CLIENT;
    case wlan_device::MacRole::MESH:
      return WLAN_INFO_MAC_ROLE_MESH;
  }
}

wlan_device::MacRole ConvertMacRole(uint16_t role) {
  switch (role) {
    case WLAN_INFO_MAC_ROLE_AP:
      return wlan_device::MacRole::AP;
    case WLAN_INFO_MAC_ROLE_CLIENT:
      return wlan_device::MacRole::CLIENT;
    case WLAN_INFO_MAC_ROLE_MESH:
      return wlan_device::MacRole::MESH;
  }
  ZX_ASSERT(0);
}

uint16_t ConvertMacRoles(const ::std::vector<wlan_device::MacRole>& roles) {
  uint16_t ret = 0;
  for (auto role : roles) {
    ret |= ConvertMacRole(role);
  }
  return ret;
}

uint32_t ConvertCaps(const ::std::vector<wlan_device::Capability>& caps) {
  uint32_t ret = 0;
  for (auto cap : caps) {
    switch (cap) {
      case wlan_device::Capability::SHORT_PREAMBLE:
        ret |= WLAN_INFO_HARDWARE_CAPABILITY_SHORT_PREAMBLE;
        break;
      case wlan_device::Capability::SPECTRUM_MGMT:
        ret |= WLAN_INFO_HARDWARE_CAPABILITY_SPECTRUM_MGMT;
        break;
      case wlan_device::Capability::QOS:
        ret |= WLAN_INFO_HARDWARE_CAPABILITY_QOS;
        break;
      case wlan_device::Capability::SHORT_SLOT_TIME:
        ret |= WLAN_INFO_HARDWARE_CAPABILITY_SHORT_SLOT_TIME;
        break;
      case wlan_device::Capability::RADIO_MSMT:
        ret |= WLAN_INFO_HARDWARE_CAPABILITY_RADIO_MSMT;
        break;
      case wlan_device::Capability::SIMULTANEOUS_CLIENT_AP:
        ret |= WLAN_INFO_HARDWARE_CAPABILITY_SIMULTANEOUS_CLIENT_AP;
        break;
    }
  }
  return ret;
}

void ConvertBandInfo(const wlan_device::BandInfo& in, wlan_info_band_info_t* out) {
  memset(out, 0, sizeof(*out));
  out->band = static_cast<uint8_t>(wlan::common::BandFromFidl(in.band_id));

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

  std::copy_n(in.rates.data(), std::min<size_t>(in.rates.size(), WLAN_INFO_BAND_INFO_MAX_RATES),
              out->rates);

  out->supported_channels.base_freq = in.supported_channels.base_freq;
  std::copy_n(
      in.supported_channels.channels.data(),
      std::min<size_t>(in.supported_channels.channels.size(), WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS),
      out->supported_channels.channels);
}

zx_status_t ConvertPhyInfo(wlan_info_t* out, const wlan_device::PhyInfo& in) {
  std::memset(out, 0, sizeof(*out));
  std::copy_n(in.hw_mac_address.begin(), ETH_MAC_SIZE, out->mac_addr);
  out->supported_phys = ConvertSupportedPhys(in.supported_phys);
  out->driver_features = ConvertDriverFeatures(in.driver_features);
  out->mac_role = ConvertMacRoles(in.mac_roles);
  out->caps = ConvertCaps(in.caps);
  out->bands_count = std::min(in.bands.size(), static_cast<size_t>(WLAN_INFO_MAX_BANDS));
  for (size_t i = 0; i < out->bands_count; ++i) {
    ConvertBandInfo((in.bands)[i], &out->bands[i]);
  }
  return ZX_OK;
}

wlan_tx_status_t ConvertTxStatus(const wlan_tap::WlanTxStatus& in) {
  wlan_tx_status_t out;
  std::copy(in.peer_addr.cbegin(), in.peer_addr.cend(), out.peer_addr);
  for (size_t i = 0; i < in.tx_status_entries.size(); ++i) {
    out.tx_status_entry[i].tx_vector_idx = in.tx_status_entries[i].tx_vec_idx;
    out.tx_status_entry[i].attempts = in.tx_status_entries[i].attempts;
  }
  out.success = in.success;
  return out;
}
}  // namespace wlan
