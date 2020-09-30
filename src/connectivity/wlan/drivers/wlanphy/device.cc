// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <fuchsia/wlan/device/cpp/fidl.h>
#include <fuchsia/wlan/device/llcpp/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <net/ethernet.h>
#include <zircon/status.h>

#include <iterator>

#include <ddk/device.h>
#include <ddk/hw/wlan/wlaninfo.h>
#include <wlan/common/band.h>
#include <wlan/common/channel.h>
#include <wlan/common/element.h>
#include <wlan/common/logging.h>
#include <wlan/common/phy.h>

#include "ddktl/fidl.h"
#include "driver.h"

namespace wlanphy {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_device = ::fuchsia::wlan::device;
namespace wlan_mlme = ::fuchsia::wlan::mlme;

class DeviceConnector : public llcpp::fuchsia::wlan::device::Connector::Interface {
 public:
  DeviceConnector(Device* device) : device_(device) {}
  void Connect(::zx::channel request, ConnectCompleter::Sync& _completer) override {
    device_->Connect(std::move(request));
  }

 private:
  Device* device_;
};

Device::Device(zx_device_t* device, wlanphy_impl_protocol_t wlanphy_impl_proto)
    : parent_(device), wlanphy_impl_(wlanphy_impl_proto), dispatcher_(wlanphy_async_t()) {
  debugfn();
  // Assert minimum required functionality from the wlanphy_impl driver
  ZX_ASSERT(wlanphy_impl_.ops != nullptr && wlanphy_impl_.ops->query != nullptr &&
            wlanphy_impl_.ops->create_iface != nullptr &&
            wlanphy_impl_.ops->destroy_iface != nullptr &&
            wlanphy_impl_.ops->set_country != nullptr && wlanphy_impl_.ops->get_country != nullptr);
}

Device::~Device() { debugfn(); }

#define DEV(c) static_cast<Device*>(c)
static zx_protocol_device_t wlanphy_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
    .message = [](void* ctx, fidl_msg_t* msg,
                  fidl_txn_t* txn) { return DEV(ctx)->Message(msg, txn); },
};
#undef DEV

zx_status_t Device::Connect(zx::channel request) {
  debugfn();
  return dispatcher_.AddBinding(std::move(request), this);
}

zx_status_t Device::Bind() {
  debugfn();

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "wlanphy";
  args.ctx = this;
  args.ops = &wlanphy_device_ops;
  args.proto_id = ZX_PROTOCOL_WLANPHY;
  zx_status_t status = device_add(parent_, &args, &zxdev_);

  if (status != ZX_OK) {
    errorf("wlanphy: could not add device: %s\n", zx_status_get_string(status));
  }

  return status;
}

zx_status_t Device::Message(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  DeviceConnector connector(this);

  llcpp::fuchsia::wlan::device::Connector::Dispatch(&connector, msg, &transaction);
  return transaction.Status();
}

void Device::Release() {
  debugfn();
  delete this;
}

void Device::Unbind() {
  debugfn();

  // Stop accepting new FIDL requests. Once the dispatcher is shut down,
  // remove the device.
  dispatcher_.InitiateShutdown([this] { device_async_remove(zxdev_); });
}

static void ConvertPhySupportedPhyInfo(::std::vector<wlan_device::SupportedPhy>* SupportedPhys,
                                       uint16_t supported_phys_mask) {
  SupportedPhys->resize(0);
  if (supported_phys_mask & WLAN_INFO_PHY_TYPE_DSSS) {
    SupportedPhys->push_back(wlan_device::SupportedPhy::DSSS);
  }
  if (supported_phys_mask & WLAN_INFO_PHY_TYPE_CCK) {
    SupportedPhys->push_back(wlan_device::SupportedPhy::CCK);
  }
  if (supported_phys_mask & WLAN_INFO_PHY_TYPE_OFDM) {
    SupportedPhys->push_back(wlan_device::SupportedPhy::OFDM);
  }
  if (supported_phys_mask & WLAN_INFO_PHY_TYPE_HT) {
    SupportedPhys->push_back(wlan_device::SupportedPhy::HT);
  }
  if (supported_phys_mask & WLAN_INFO_PHY_TYPE_VHT) {
    SupportedPhys->push_back(wlan_device::SupportedPhy::VHT);
  }
}

static void ConvertPhyDriverFeaturesInfo(::std::vector<wlan_common::DriverFeature>* DriverFeatures,
                                         uint32_t driver_features_mask) {
  DriverFeatures->resize(0);
  if (driver_features_mask & WLAN_INFO_DRIVER_FEATURE_SCAN_OFFLOAD) {
    DriverFeatures->push_back(wlan_common::DriverFeature::SCAN_OFFLOAD);
  }
  if (driver_features_mask & WLAN_INFO_DRIVER_FEATURE_RATE_SELECTION) {
    DriverFeatures->push_back(wlan_common::DriverFeature::RATE_SELECTION);
  }
  if (driver_features_mask & WLAN_INFO_DRIVER_FEATURE_SYNTH) {
    DriverFeatures->push_back(wlan_common::DriverFeature::SYNTH);
  }
  if (driver_features_mask & WLAN_INFO_DRIVER_FEATURE_TX_STATUS_REPORT) {
    DriverFeatures->push_back(wlan_common::DriverFeature::TX_STATUS_REPORT);
  }
  if (driver_features_mask & WLAN_INFO_DRIVER_FEATURE_PROBE_RESP_OFFLOAD) {
    DriverFeatures->push_back(wlan_common::DriverFeature::PROBE_RESP_OFFLOAD);
  }
  if (driver_features_mask & WLAN_INFO_DRIVER_FEATURE_SAE_SME_AUTH) {
    DriverFeatures->push_back(wlan_common::DriverFeature::SAE_SME_AUTH);
  }
  if (driver_features_mask & WLAN_INFO_DRIVER_FEATURE_SAE_DRIVER_AUTH) {
    DriverFeatures->push_back(wlan_common::DriverFeature::SAE_DRIVER_AUTH);
  }
  if (driver_features_mask & WLAN_INFO_DRIVER_FEATURE_MFP) {
    DriverFeatures->push_back(wlan_common::DriverFeature::MFP);
  }
}

void ConvertPhyRolesInfo(::std::vector<wlan_device::MacRole>* MacRoles,
                         wlan_info_mac_role_t mac_roles_mask) {
  MacRoles->resize(0);
  if (mac_roles_mask & WLAN_INFO_MAC_ROLE_CLIENT) {
    MacRoles->push_back(wlan_device::MacRole::CLIENT);
  }
  if (mac_roles_mask & WLAN_INFO_MAC_ROLE_AP) {
    MacRoles->push_back(wlan_device::MacRole::AP);
  }
  if (mac_roles_mask & WLAN_INFO_MAC_ROLE_MESH) {
    MacRoles->push_back(wlan_device::MacRole::MESH);
  }
}

void ConvertPhyCaps(::std::vector<wlan_device::Capability>* Capabilities, uint32_t phy_caps_mask) {
  Capabilities->resize(0);
  if (phy_caps_mask & WLAN_INFO_HARDWARE_CAPABILITY_SHORT_PREAMBLE) {
    Capabilities->push_back(wlan_device::Capability::SHORT_PREAMBLE);
  }
  if (phy_caps_mask & WLAN_INFO_HARDWARE_CAPABILITY_SPECTRUM_MGMT) {
    Capabilities->push_back(wlan_device::Capability::SPECTRUM_MGMT);
  }
  if (phy_caps_mask & WLAN_INFO_HARDWARE_CAPABILITY_SHORT_SLOT_TIME) {
    Capabilities->push_back(wlan_device::Capability::SHORT_SLOT_TIME);
  }
  if (phy_caps_mask & WLAN_INFO_HARDWARE_CAPABILITY_RADIO_MSMT) {
    Capabilities->push_back(wlan_device::Capability::RADIO_MSMT);
  }
  if (phy_caps_mask & WLAN_INFO_HARDWARE_CAPABILITY_SIMULTANEOUS_CLIENT_AP) {
    Capabilities->push_back(wlan_device::Capability::SIMULTANEOUS_CLIENT_AP);
  }
}

static void ConvertPhyChannels(wlan_device::ChannelList* Channels,
                               const wlan_info_channel_list_t* phy_channels) {
  // base_freq
  Channels->base_freq = phy_channels->base_freq;

  // channels
  Channels->channels.resize(0);
  size_t channel_ndx = 0;
  while ((channel_ndx < std::size(phy_channels->channels)) &&
         (phy_channels->channels[channel_ndx] > 0)) {
    Channels->channels.push_back(phy_channels->channels[channel_ndx]);
    channel_ndx++;
  }
}

void ConvertPhyBandInfo(::std::vector<wlan_device::BandInfo>* BandInfo, uint8_t bands_count,
                        const wlan_info_band_info_t* all_phy_bands) {
  BandInfo->resize(0);
  for (uint8_t band_num = 0; band_num < bands_count; band_num++) {
    wlan_device::BandInfo out_band{};
    const wlan_info_band_info_t& this_phy_band = all_phy_bands[band_num];
    out_band.band_id = wlan::common::BandToFidl(this_phy_band.band);

    // ht_caps
    if (this_phy_band.ht_supported) {
      out_band.ht_caps = wlan_mlme::HtCapabilities::New();
      auto ht_cap = ::wlan::HtCapabilities::FromDdk(this_phy_band.ht_caps);
      static_assert(sizeof(out_band.ht_caps->bytes) == sizeof(ht_cap));
      memcpy(out_band.ht_caps->bytes.data(), &ht_cap, sizeof(ht_cap));
    }

    // vht_caps
    if (this_phy_band.vht_supported) {
      out_band.vht_caps = wlan_mlme::VhtCapabilities::New();
      auto vht_cap = ::wlan::VhtCapabilities::FromDdk(this_phy_band.vht_caps);
      static_assert(sizeof(out_band.vht_caps->bytes) == sizeof(vht_cap));
      memcpy(out_band.vht_caps->bytes.data(), &vht_cap, sizeof(vht_cap));
    }

    // rates
    out_band.rates.resize(0);
    size_t rate_ndx = 0;
    while ((rate_ndx < std::size(this_phy_band.rates)) && (this_phy_band.rates[rate_ndx] > 0)) {
      out_band.rates.push_back(this_phy_band.rates[rate_ndx]);
      rate_ndx++;
    }

    // supported_channels
    ConvertPhyChannels(&out_band.supported_channels, &this_phy_band.supported_channels);

    BandInfo->push_back(std::move(out_band));
  }
}

static void ConvertPhyInfo(wlan_device::PhyInfo* info, const wlan_info_t* phy_info) {
  // mac
  memcpy(info->hw_mac_address.data(), phy_info->mac_addr, ETH_ALEN);

  // supported_phys
  ConvertPhySupportedPhyInfo(&info->supported_phys, phy_info->supported_phys);

  // driver_features
  ConvertPhyDriverFeaturesInfo(&info->driver_features, phy_info->driver_features);

  // mac_roles
  ConvertPhyRolesInfo(&info->mac_roles, phy_info->mac_role);

  // caps
  ConvertPhyCaps(&info->caps, phy_info->caps);

  // bands
  ConvertPhyBandInfo(&info->bands, phy_info->bands_count, phy_info->bands);
}

void Device::Query(QueryCallback callback) {
  debugfn();
  wlan_device::QueryResponse resp;
  wlanphy_impl_info_t phy_impl_info;
  resp.status = wlanphy_impl_.ops->query(wlanphy_impl_.ctx, &phy_impl_info);
  ConvertPhyInfo(&resp.info, &phy_impl_info.wlan_info);
  callback(std::move(resp));
}

void Device::CreateIface(wlan_device::CreateIfaceRequest req, CreateIfaceCallback callback) {
  debugfn();
  wlan_device::CreateIfaceResponse resp;

  wlan_info_mac_role_t role = 0;
  switch (req.role) {
    case wlan_device::MacRole::CLIENT:
      role = WLAN_INFO_MAC_ROLE_CLIENT;
      break;
    case wlan_device::MacRole::AP:
      role = WLAN_INFO_MAC_ROLE_AP;
      break;
    case wlan_device::MacRole::MESH:
      role = WLAN_INFO_MAC_ROLE_MESH;
      break;
  }

  if (role != 0) {
    uint16_t iface_id;
    wlanphy_impl_create_iface_req_t create_req{.role = role,
                                               .sme_channel = req.sme_channel.release()};
    if (req.init_mac_addr.has_value()) {
      create_req.has_init_mac_addr = true;
      std::copy(req.init_mac_addr.value().begin(), req.init_mac_addr.value().end(),
                create_req.init_mac_addr);
    } else {
      create_req.has_init_mac_addr = false;
    }

    resp.status = wlanphy_impl_.ops->create_iface(wlanphy_impl_.ctx, &create_req, &iface_id);
    resp.iface_id = iface_id;
  } else {
    resp.status = ZX_ERR_NOT_SUPPORTED;
  }

  callback(std::move(resp));
}

void Device::DestroyIface(wlan_device::DestroyIfaceRequest req, DestroyIfaceCallback callback) {
  debugfn();
  wlan_device::DestroyIfaceResponse resp;
  resp.status = wlanphy_impl_.ops->destroy_iface(wlanphy_impl_.ctx, req.id);
  callback(std::move(resp));
}

void Device::SetCountry(wlan_device::CountryCode req, SetCountryCallback callback) {
  debugfn();
  debugf("wlanphy: SetCountry to %s\n", wlan::common::Alpha2ToStr(req.alpha2).c_str());

  wlanphy_country_t country;
  memcpy(country.alpha2, req.alpha2.data(), WLANPHY_ALPHA2_LEN);
  auto status = wlanphy_impl_.ops->set_country(wlanphy_impl_.ctx, &country);

  if (status != ZX_OK) {
    debugf("wlanphy: SetCountry to %s failed with error %s\n",
           wlan::common::Alpha2ToStr(req.alpha2).c_str(), zx_status_get_string(status));
  }
  callback(status);
}

void Device::GetCountry(GetCountryCallback callback) {
  debugfn();

  wlanphy_country_t country;
  auto status = wlanphy_impl_.ops->get_country(wlanphy_impl_.ctx, &country);
  if (status != ZX_OK) {
    debugf("wlanphy: GetCountry failed with error %s\n", zx_status_get_string(status));
    callback(fit::error(status));
  } else {
    wlan_device::CountryCode resp;
    memcpy(resp.alpha2.data(), country.alpha2, WLANPHY_ALPHA2_LEN);
    debugf("wlanphy: GetCountry returning %s\n", wlan::common::Alpha2ToStr(resp.alpha2).c_str());
    callback(fit::ok(std::move(resp)));
  }
}

void Device::ClearCountry(ClearCountryCallback callback) {
  debugfn();
  auto status = wlanphy_impl_.ops->clear_country(wlanphy_impl_.ctx);
  if (status != ZX_OK) {
    debugf("wlanphy: ClearCountry failed with error %s\n", zx_status_get_string(status));
  }
  callback(status);
}

}  // namespace wlanphy
