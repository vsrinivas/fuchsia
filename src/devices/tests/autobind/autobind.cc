// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/autobind/autobind.h"

#include "src/devices/tests/autobind/autobind-bind.h"

namespace auto_bind {

zx_status_t AutoBind::Bind(void* ctx, zx_device_t* dev) {
  auto device = std::make_unique<AutoBind>(dev);
  zx_status_t status = device->Bind();
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto ptr = device.release();
  return ZX_OK;
}

zx_status_t AutoBind::Bind() {
  is_bound.Set(true);
  zx_device_prop_t props[] = {
      {BIND_PCI_VID, 0, 3},
  };
  uint32_t flags = DEVICE_ADD_NON_BINDABLE;
  return DdkAdd(ddk::DeviceAddArgs("autobind")
                    .set_props(props)
                    .set_flags(flags)
                    .set_inspect_vmo(inspect_.DuplicateVmo()));
}

void AutoBind::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void AutoBind::DdkRelease() { delete this; }

static zx_driver_ops_t auto_bind_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AutoBind::Bind;
  return ops;
}();

}  // namespace auto_bind

ZIRCON_DRIVER(AutoBind, auto_bind::auto_bind_driver_ops, "zircon", "0.1");
