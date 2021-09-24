// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <fidl/fuchsia.wlan.device/cpp/wire.h>
#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>
#include <fuchsia/wlan/device/cpp/fidl.h>
#include <fuchsia/wlan/internal/cpp/fidl.h>
#include <lib/ddk/device.h>
#include <net/ethernet.h>
#include <zircon/status.h>

#include <iterator>

#include <ddk/hw/wlan/wlaninfo/c/banjo.h>
#include <wlan/common/band.h>
#include <wlan/common/channel.h>
#include <wlan/common/element.h>
#include <wlan/common/phy.h>

#include "ddktl/fidl.h"
#include "debug.h"
#include "driver.h"

namespace wlanphy {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_device = ::fuchsia::wlan::device;
namespace wlan_internal = ::fuchsia::wlan::internal;

class DeviceConnector : public fidl::WireServer<fuchsia_wlan_device::Connector> {
 public:
  DeviceConnector(Device* device) : device_(device) {}
  void Connect(ConnectRequestView request, ConnectCompleter::Sync& _completer) override {
    device_->Connect(request->request.TakeChannel());
  }

 private:
  Device* device_;
};

Device::Device(zx_device_t* device, wlanphy_impl_protocol_t wlanphy_impl_proto)
    : parent_(device), wlanphy_impl_(wlanphy_impl_proto), dispatcher_(wlanphy_async_t()) {
  ltrace_fn();
  // Assert minimum required functionality from the wlanphy_impl driver
  ZX_ASSERT(wlanphy_impl_.ops != nullptr && wlanphy_impl_.ops->query != nullptr &&
            wlanphy_impl_.ops->create_iface != nullptr &&
            wlanphy_impl_.ops->destroy_iface != nullptr &&
            wlanphy_impl_.ops->set_country != nullptr && wlanphy_impl_.ops->get_country != nullptr);
}

Device::~Device() { ltrace_fn(); }

#define DEV(c) static_cast<Device*>(c)
static zx_protocol_device_t wlanphy_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
    .message = [](void* ctx, fidl_incoming_msg_t* msg,
                  fidl_txn_t* txn) { return DEV(ctx)->Message(msg, txn); },
};
#undef DEV

zx_status_t Device::Connect(zx::channel request) {
  ltrace_fn();
  return dispatcher_.AddBinding(std::move(request), this);
}

zx_status_t Device::Bind() {
  ltrace_fn();

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "wlanphy";
  args.ctx = this;
  args.ops = &wlanphy_device_ops;
  args.proto_id = ZX_PROTOCOL_WLANPHY;
  zx_status_t status = device_add(parent_, &args, &zxdev_);

  if (status != ZX_OK) {
    lerror("could not add device: %s\n", zx_status_get_string(status));
  }

  return status;
}

zx_status_t Device::Message(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  DeviceConnector connector(this);

  fidl::WireDispatch<fuchsia_wlan_device::Connector>(
      &connector, fidl::IncomingMessage::FromEncodedCMessage(msg), &transaction);
  return transaction.Status();
}

void Device::Release() {
  ltrace_fn();
  delete this;
}

void Device::Unbind() {
  ltrace_fn();

  // Stop accepting new FIDL requests. Once the dispatcher is shut down,
  // remove the device.
  dispatcher_.InitiateShutdown([this] { device_async_remove(zxdev_); });
}

void ConvertSupportedMacRoles(::std::vector<wlan_device::MacRole>* fidl_supported_mac_roles,
                              wlan_info_mac_role_t banjo_supported_mac_roles) {
  fidl_supported_mac_roles->resize(0);
  if (banjo_supported_mac_roles & WLAN_INFO_MAC_ROLE_CLIENT) {
    fidl_supported_mac_roles->push_back(wlan_device::MacRole::CLIENT);
  }
  if (banjo_supported_mac_roles & WLAN_INFO_MAC_ROLE_AP) {
    fidl_supported_mac_roles->push_back(wlan_device::MacRole::AP);
  }
  if (banjo_supported_mac_roles & WLAN_INFO_MAC_ROLE_MESH) {
    fidl_supported_mac_roles->push_back(wlan_device::MacRole::MESH);
  }
}

static void ConvertPhyInfo(wlan_device::PhyInfo* fidl_info, const wlanphy_impl_info_t* banjo_info) {
  // supported_mac_roles
  ConvertSupportedMacRoles(&fidl_info->supported_mac_roles, banjo_info->supported_mac_roles);
}

void Device::Query(QueryCallback callback) {
  ltrace_fn();
  wlan_device::QueryResponse resp;
  wlanphy_impl_info_t phy_impl_info;
  resp.status = wlanphy_impl_.ops->query(wlanphy_impl_.ctx, &phy_impl_info);
  ConvertPhyInfo(&resp.info, &phy_impl_info);
  callback(std::move(resp));
}

const std::array<uint8_t, 6> NULL_MAC_ADDR{0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void Device::CreateIface(wlan_device::CreateIfaceRequest req, CreateIfaceCallback callback) {
  ltrace_fn();
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
                                               .mlme_channel = req.mlme_channel.release()};
    if (req.init_sta_addr != NULL_MAC_ADDR) {
      create_req.has_init_sta_addr = true;
      std::copy(req.init_sta_addr.begin(), req.init_sta_addr.end(), create_req.init_sta_addr);
    } else {
      create_req.has_init_sta_addr = false;
    }

    resp.status = wlanphy_impl_.ops->create_iface(wlanphy_impl_.ctx, &create_req, &iface_id);
    resp.iface_id = iface_id;
  } else {
    resp.status = ZX_ERR_NOT_SUPPORTED;
  }

  callback(std::move(resp));
}

void Device::DestroyIface(wlan_device::DestroyIfaceRequest req, DestroyIfaceCallback callback) {
  ltrace_fn();
  wlan_device::DestroyIfaceResponse resp;
  resp.status = wlanphy_impl_.ops->destroy_iface(wlanphy_impl_.ctx, req.id);
  callback(std::move(resp));
}

void Device::SetCountry(wlan_device::CountryCode req, SetCountryCallback callback) {
  ltrace_fn();
  ldebug_device("SetCountry to %s\n", wlan::common::Alpha2ToStr(req.alpha2).c_str());

  wlanphy_country_t country;
  memcpy(country.alpha2, req.alpha2.data(), WLANPHY_ALPHA2_LEN);
  auto status = wlanphy_impl_.ops->set_country(wlanphy_impl_.ctx, &country);

  if (status != ZX_OK) {
    ldebug_device("SetCountry to %s failed with error %s\n",
                  wlan::common::Alpha2ToStr(req.alpha2).c_str(), zx_status_get_string(status));
  }
  callback(status);
}

void Device::GetCountry(GetCountryCallback callback) {
  ltrace_fn();

  wlanphy_country_t country;
  auto status = wlanphy_impl_.ops->get_country(wlanphy_impl_.ctx, &country);
  if (status != ZX_OK) {
    ldebug_device("GetCountry failed with error %s\n", zx_status_get_string(status));
    callback(fpromise::error(status));
  } else {
    wlan_device::CountryCode resp;
    memcpy(resp.alpha2.data(), country.alpha2, WLANPHY_ALPHA2_LEN);
    ldebug_device("GetCountry returning %s\n", wlan::common::Alpha2ToStr(resp.alpha2).c_str());
    callback(fpromise::ok(std::move(resp)));
  }
}

void Device::ClearCountry(ClearCountryCallback callback) {
  ltrace_fn();
  auto status = wlanphy_impl_.ops->clear_country(wlanphy_impl_.ctx);
  if (status != ZX_OK) {
    ldebug_device("ClearCountry failed with error %s\n", zx_status_get_string(status));
  }
  callback(status);
}

}  // namespace wlanphy
