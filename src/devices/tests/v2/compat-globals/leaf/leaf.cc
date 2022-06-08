// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header has to come first, and we define our ZX_PROTOCOL, so that
// we don't have to edit protodefs.h to add this test protocol.

#include <bind/fuchsia/compat/cpp/bind.h>
#define ZX_PROTOCOL_PARENT bind_fuchsia_compat::BIND_PROTOCOL_PARENT

#include <fidl/fuchsia.hardware.compat/cpp/wire.h>
#include <fuchsia/hardware/compat/cpp/banjo.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>

#include "src/devices/tests/v2/compat-globals/leaf/leaf-bind.h"

namespace leaf {

uint32_t global_num = 0;

class LeafChild;
using LeafChildDeviceType = ddk::Device<LeafChild>;
class LeafChild : public LeafChildDeviceType {
 public:
  LeafChild(zx_device_t* device) : LeafChildDeviceType(device) {}

  static zx_status_t Bind(zx_device_t* device, std::string_view name) {
    auto driver = std::make_unique<LeafChild>(device);

    zx_status_t status = driver->DdkAdd(ddk::DeviceAddArgs(name.data()));
    if (status != ZX_OK) {
      return status;
    }

    // The DriverFramework now owns driver.
    __UNUSED auto ptr = driver.release();
    return ZX_OK;
  }

  void DdkRelease() { delete this; }
};

class Leaf;
using DeviceType =
    ddk::Device<Leaf, ddk::Initializable, ddk::Messageable<fuchsia_hardware_compat::Leaf>::Mixin>;
class Leaf : public DeviceType {
 public:
  explicit Leaf(zx_device_t* root) : DeviceType(root) {}
  virtual ~Leaf() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev) {
    parent_protocol_t compat_root;
    if (device_get_protocol(dev, ZX_PROTOCOL_PARENT, &compat_root)) {
      zxlogf(ERROR, "leaf: bind: no Root protocol");
      return ZX_ERR_INTERNAL;
    }

    auto driver = std::make_unique<Leaf>(dev);
    ddk::ParentProtocolClient client = ddk::ParentProtocolClient(&compat_root);
    zx_status_t status = driver->Bind(std::move(client));
    if (status != ZX_OK) {
      return status;
    }
    // The DriverFramework now owns driver.
    __UNUSED auto ptr = driver.release();
    return ZX_OK;
  }

  zx_status_t Bind(ddk::ParentProtocolClient client) {
    client_ = std::move(client);
    return DdkAdd(ddk::DeviceAddArgs("leaf").set_flags(DEVICE_ADD_NON_BINDABLE));
  }

  void DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

  void DdkRelease() { delete this; }

  void GetString(GetStringRequestView request, GetStringCompleter::Sync& completer) override {
    char str[100];
    client_.GetString(str, 100);
    completer.Reply(fidl::StringView::FromExternal(std::string(str)));
  }

  void AddChild(AddChildRequestView request, AddChildCompleter::Sync& completer) override {
    zx_status_t status = LeafChild::Bind(zxdev(), request->name.get());
    completer.Reply(status);
  }

  void GlobalCounter(GlobalCounterRequestView request,
                     GlobalCounterCompleter::Sync& completer) override {
    completer.Reply(global_num++);
  }

 private:
  ddk::ParentProtocolClient client_;
};

static zx_driver_ops_t leaf_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Leaf::Bind;
  return ops;
}();

}  // namespace leaf

ZIRCON_DRIVER(Leaf, leaf::leaf_driver_ops, "zircon", "0.1");
