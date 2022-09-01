// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "phy-device.h"

#include <fidl/fuchsia.wlan.device/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <stdio.h>

#include <algorithm>

#include <wlan/common/element.h>
#include <wlan/common/phy.h>

#include "ddktl/fidl.h"
#include "driver.h"
#include "iface-device.h"

namespace wlan {
namespace testing {

#define DEV(c) (static_cast<PhyDevice*>(c))
static zx_protocol_device_t wlanphy_test_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
    .message = [](void* ctx, fidl_incoming_msg_t* msg,
                  fidl_txn_t* txn) { return DEV(ctx)->Message(msg, txn); },
};
#undef DEV

class DeviceConnector : public fidl::WireServer<fuchsia_wlan_device::Connector> {
 public:
  explicit DeviceConnector(PhyDevice* device) : device_(device) {}
  void Connect(ConnectRequestView request, ConnectCompleter::Sync& _completer) override {
    device_->Connect(std::move(request->request));
  }

 private:
  PhyDevice* device_;
};

PhyDevice::PhyDevice(zx_device_t* device) : parent_(device) {}

zx_status_t PhyDevice::Bind() {
  zxlogf(INFO, "wlan::testing::phy::PhyDevice::Bind()");

  dispatcher_ =
      std::make_unique<wlan::common::Dispatcher<fuchsia_wlan_device::Phy>>(wlanphy_async_t());

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "wlanphy-test";
  args.ctx = this;
  args.ops = &wlanphy_test_device_ops;
  args.proto_id = ZX_PROTOCOL_WLANPHY;

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

  fidl::WireDispatch<fuchsia_wlan_device::Connector>(
      &connector, fidl::IncomingHeaderAndMessage::FromEncodedCMessage(msg), &transaction);
  return transaction.Status();
}

void PhyDevice::GetSupportedMacRoles(GetSupportedMacRolesCompleter::Sync& completer) {
  zxlogf(INFO, "wlan::testing::phy::PhyDevice::GetSupportedMacRoles()");

  fuchsia_wlan_common::WlanMacRole roles[] = {fuchsia_wlan_common::WlanMacRole::kClient,
                                              fuchsia_wlan_common::WlanMacRole::kAp};
  completer.ReplySuccess(fidl::VectorView<fuchsia_wlan_common::WlanMacRole>::FromExternal(roles));
}

void PhyDevice::CreateIface(CreateIfaceRequestView req, CreateIfaceCompleter::Sync& completer) {
  zxlogf(INFO, "CreateRequest: role=%u", req->req.role);
  std::lock_guard<std::mutex> guard(lock_);

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
    completer.ReplyError(ZX_ERR_NO_RESOURCES);
    return;
  }

  wlan_mac_role_t role = 0;
  switch (req->req.role) {
    case fuchsia_wlan_common::WlanMacRole::kClient:
      role = WLAN_MAC_ROLE_CLIENT;
      break;
    case fuchsia_wlan_common::WlanMacRole::kAp:
      role = WLAN_MAC_ROLE_AP;
      break;
    case fuchsia_wlan_common::WlanMacRole::kMesh:
      role = WLAN_MAC_ROLE_MESH;
      break;
    default:
      completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
      return;
  }

  // Create the interface device and bind it.
  auto macdev = std::make_unique<IfaceDevice>(zxdev_, role);
  if (zx_status_t status = macdev->Bind(); status != ZX_OK) {
    zxlogf(ERROR, "could not bind child wlan-softmac device: %d", status);
    completer.ReplyError(status);
    return;
  }

  // Memory management follows the device lifecycle at this point. The only way an interface can be
  // removed is through this phy device, either through a DestroyIface call or by the phy going
  // away, so it should be safe to store the raw pointer.
  ifaces_[id] = macdev.release();

  // Since we successfully used the id, increment the next id counter.
  next_id_ = id + 1;

  completer.ReplySuccess(id);
}

void PhyDevice::DestroyIface(DestroyIfaceRequestView req, DestroyIfaceCompleter::Sync& completer) {
  zxlogf(INFO, "DestroyRequest: id=%u", req->req.id);

  std::lock_guard<std::mutex> guard(lock_);
  auto intf = ifaces_.find(req->req.id);
  if (intf == ifaces_.end()) {
    completer.ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }

  device_async_remove(intf->second->zxdev());
  // Remove the device from our map. We do NOT free the memory, since the devhost owns it and will
  // call release when it's safe to free the memory.
  ifaces_.erase(intf);

  completer.ReplySuccess();
}

void PhyDevice::SetCountry(SetCountryRequestView req, SetCountryCompleter::Sync& completer) {
  zxlogf(INFO, "testing/PHY: SetCountry [%s]", wlan::common::Alpha2ToStr(req->req.alpha2).c_str());
  completer.Reply(ZX_OK);
}

void PhyDevice::GetCountry(GetCountryCompleter::Sync& completer) {
  zxlogf(INFO, "testing/PHY: GetCountry");
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void PhyDevice::ClearCountry(ClearCountryCompleter::Sync& completer) {
  zxlogf(INFO, "testing/PHY: ClearCountry");
  completer.Reply(ZX_OK);
}

void PhyDevice::SetPsMode(SetPsModeRequestView req, SetPsModeCompleter::Sync& completer) {
  zxlogf(INFO, "testing/PHY: SetPsMode [%d]", req->req);
  completer.Reply(ZX_OK);
}

void PhyDevice::GetPsMode(GetPsModeCompleter::Sync& completer) {
  zxlogf(INFO, "testing/PHY: GetPSMode");
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

zx_status_t PhyDevice::Connect(fidl::ServerEnd<fuchsia_wlan_device::Phy> server_end) {
  return dispatcher_->AddBinding(std::move(server_end), this);
}

}  // namespace testing
}  // namespace wlan
