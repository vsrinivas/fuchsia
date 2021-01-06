// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "phy-device.h"

#include <fuchsia/wlan/device/llcpp/fidl.h>
#include <fuchsia/wlan/internal/cpp/fidl.h>
#include <stdio.h>

#include <algorithm>

#include <ddk/debug.h>
#include <ddk/protocol/wlanphy.h>
#include <wlan/common/element.h>
#include <wlan/common/phy.h>

#include "ddktl/fidl.h"
#include "driver.h"
#include "iface-device.h"

namespace wlan {
namespace testing {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_device = ::fuchsia::wlan::device;
namespace wlan_internal = ::fuchsia::wlan::internal;

#define DEV(c) static_cast<PhyDevice*>(c)
static zx_protocol_device_t wlanphy_test_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
    .message = [](void* ctx, fidl_incoming_msg_t* msg,
                  fidl_txn_t* txn) { return DEV(ctx)->Message(msg, txn); },
};
#undef DEV

static wlanphy_protocol_ops_t wlanphy_test_ops = {
    .dummy = nullptr,
};

class DeviceConnector : public llcpp::fuchsia::wlan::device::Connector::Interface {
 public:
  DeviceConnector(PhyDevice* device) : device_(device) {}
  void Connect(::zx::channel request, ConnectCompleter::Sync& _completer) override {
    device_->Connect(std::move(request));
  }

 private:
  PhyDevice* device_;
};

PhyDevice::PhyDevice(zx_device_t* device) : parent_(device) {}

zx_status_t PhyDevice::Bind() {
  zxlogf(INFO, "wlan::testing::phy::PhyDevice::Bind()");

  dispatcher_ = std::make_unique<wlan::common::Dispatcher<wlan_device::Phy>>(wlanphy_async_t());

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "wlanphy-test";
  args.ctx = this;
  args.ops = &wlanphy_test_device_ops;
  args.proto_id = ZX_PROTOCOL_WLANPHY;
  args.proto_ops = &wlanphy_test_ops;

  zx_status_t status = device_add(parent_, &args, &zxdev_);
  if (status != ZX_OK) {
    printf("wlanphy-test: could not add test device: %d\n", status);
  }

  return status;
}

void PhyDevice::Unbind() {
  zxlogf(INFO, "wlan::testing::PhyDevice::Unbind()");
  std::lock_guard<std::mutex> guard(lock_);
  dispatcher_.reset();
  device_unbind_reply(zxdev_);
}

void PhyDevice::Release() {
  zxlogf(INFO, "wlan::testing::PhyDevice::Release()");
  delete this;
}

zx_status_t PhyDevice::Message(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  DeviceConnector connector(this);

  llcpp::fuchsia::wlan::device::Connector::Dispatch(&connector, msg, &transaction);
  return transaction.Status();
}

namespace {
wlan_device::PhyInfo get_info() {
  wlan_device::PhyInfo info;

  info.supported_mac_roles.resize(0);

  info.supported_mac_roles.push_back(wlan_device::MacRole::CLIENT);
  info.supported_mac_roles.push_back(wlan_device::MacRole::AP);

  return info;
}
}  // namespace

void PhyDevice::Query(QueryCallback callback) {
  zxlogf(INFO, "wlan::testing::phy::PhyDevice::Query()");
  wlan_device::QueryResponse resp;
  resp.info = get_info();
  callback(std::move(resp));
}

void PhyDevice::CreateIface(wlan_device::CreateIfaceRequest req, CreateIfaceCallback callback) {
  zxlogf(INFO, "CreateRequest: role=%u", req.role);
  std::lock_guard<std::mutex> guard(lock_);
  wlan_device::CreateIfaceResponse resp;

  // We leverage wrapping of unsigned ints to cycle back through ids to find an unused one.
  bool found_unused = false;
  uint16_t id = next_id_;
  while (!found_unused) {
    if (ifaces_.count(id) > 0) {
      id++;
      // If we wrap all the way around, something is very wrong.
      if (next_id_ == id) {
        break;
      }
    } else {
      found_unused = true;
    }
  }
  ZX_DEBUG_ASSERT(found_unused);
  if (!found_unused) {
    resp.status = ZX_ERR_NO_RESOURCES;
    callback(std::move(resp));
    return;
  }

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
    default:
      resp.status = ZX_ERR_NOT_SUPPORTED;
      callback(std::move(resp));
      return;
  }

  // Create the interface device and bind it.
  auto macdev = std::make_unique<IfaceDevice>(zxdev_, role);
  zx_status_t status = macdev->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not bind child wlanmac device: %d", status);
    resp.status = status;
    callback(std::move(resp));
    return;
  }

  // Memory management follows the device lifecycle at this point. The only way an interface can be
  // removed is through this phy device, either through a DestroyIface call or by the phy going
  // away, so it should be safe to store the raw pointer.
  ifaces_[id] = macdev.release();

  // Since we successfully used the id, increment the next id counter.
  next_id_ = id + 1;

  resp.iface_id = id;
  resp.status = ZX_OK;
  callback(std::move(resp));
}

void PhyDevice::DestroyIface(wlan_device::DestroyIfaceRequest req, DestroyIfaceCallback callback) {
  zxlogf(INFO, "DestroyRequest: id=%u", req.id);

  wlan_device::DestroyIfaceResponse resp;

  std::lock_guard<std::mutex> guard(lock_);
  auto intf = ifaces_.find(req.id);
  if (intf == ifaces_.end()) {
    resp.status = ZX_ERR_NOT_FOUND;
    callback(std::move(resp));
    return;
  }

  device_async_remove(intf->second->zxdev());
  // Remove the device from our map. We do NOT free the memory, since the devhost owns it and will
  // call release when it's safe to free the memory.
  ifaces_.erase(req.id);

  resp.status = ZX_OK;
  callback(std::move(resp));
}

void PhyDevice::SetCountry(wlan_device::CountryCode req, SetCountryCallback callback) {
  zxlogf(INFO, "testing/PHY: SetCountry [%s]", wlan::common::Alpha2ToStr(req.alpha2).c_str());
  callback(ZX_OK);
}

void PhyDevice::GetCountry(GetCountryCallback callback) {
  zxlogf(INFO, "testing/PHY: GetCountry");
  callback(fit::error(ZX_ERR_NOT_SUPPORTED));
}

void PhyDevice::ClearCountry(ClearCountryCallback callback) {
  zxlogf(INFO, "testing/PHY: ClearCountry");
  callback(ZX_OK);
}

zx_status_t PhyDevice::Connect(zx::channel request) {
  return dispatcher_->AddBinding(std::move(request), this);
}

}  // namespace testing
}  // namespace wlan
