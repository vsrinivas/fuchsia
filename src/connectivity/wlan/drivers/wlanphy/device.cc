// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <fuchsia/wlan/device/cpp/fidl.h>
#include <fuchsia/wlan/device/llcpp/fidl.h>
#include <fuchsia/wlan/internal/cpp/fidl.h>
#include <net/ethernet.h>
#include <zircon/status.h>

#include <iterator>

#include <ddk/device.h>
#include <ddk/hw/wlan/wlaninfo.h>
#include <ddk/protocol/wlanphyimpl.h>
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
namespace wlan_internal = ::fuchsia::wlan::internal;

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
    .message = [](void* ctx, fidl_incoming_msg_t* msg,
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

zx_status_t Device::Message(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
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

static void ConvertPhyInfo(wlan_device::PhyInfo* info, const wlanphy_impl_info_t* phy_info) {
  // supported_mac_roles
  ConvertPhyRolesInfo(&info->supported_mac_roles, phy_info->supported_mac_roles);
}

void Device::Query(QueryCallback callback) {
  debugfn();
  wlan_device::QueryResponse resp;
  wlanphy_impl_info_t phy_impl_info;
  resp.status = wlanphy_impl_.ops->query(wlanphy_impl_.ctx, &phy_impl_info);
  ConvertPhyInfo(&resp.info, &phy_impl_info);
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
