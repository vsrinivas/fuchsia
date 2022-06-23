// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/nonbindable/nonbindable.h"

#include "src/devices/tests/nonbindable/nonbindable-bind.h"

namespace auto_bind {

class Child;
using ChildDeviceType = ddk::Device<Child>;
class Child : public ChildDeviceType {
 public:
  explicit Child(zx_device_t* parent) : ChildDeviceType(parent) {}
  virtual ~Child() = default;

  zx_status_t Bind() { return DdkAdd(ddk::DeviceAddArgs("child")); }

  void DdkRelease() { delete this; }
};

zx_status_t Nonbindable::Bind(void* ctx, zx_device_t* dev) {
  auto device = std::make_unique<Nonbindable>(dev);
  zx_status_t status = device->Bind();
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto ptr = device.release();
  return ZX_OK;
}

zx_status_t Nonbindable::Bind() {
  constexpr uint32_t flags = DEVICE_ADD_NON_BINDABLE;
  return DdkAdd(ddk::DeviceAddArgs("nonbindable").set_flags(flags));
}

void Nonbindable::DdkInit(ddk::InitTxn txn) {
  auto child = std::make_unique<Child>(zxdev());
  zx_status_t status = child->Bind();
  if (status != ZX_OK) {
    txn.Reply(status);
  }

  __UNUSED auto ptr = child.release();

  txn.Reply(ZX_OK);
}

void Nonbindable::DdkRelease() { delete this; }

static zx_driver_ops_t auto_bind_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Nonbindable::Bind;
  return ops;
}();

}  // namespace auto_bind

ZIRCON_DRIVER(Nonbindable, auto_bind::auto_bind_driver_ops, "zircon", "0.1");
