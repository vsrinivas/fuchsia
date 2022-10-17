// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>

#include "src/devices/tests/v2/init-child-first/init-test-bind.h"

namespace init_test {

class InitTestChild;
using InitChildDeviceType = ddk::Device<InitTestChild, ddk::Initializable>;
class InitTestChild : public InitChildDeviceType {
 public:
  explicit InitTestChild(zx_device_t* parent) : InitChildDeviceType(parent) {}
  static zx::result<InitTestChild*> Create(zx_device_t* parent, const char* name) {
    auto driver = std::make_unique<InitTestChild>(parent);
    zx_status_t status = driver->DdkAdd(ddk::DeviceAddArgs(name));
    if (status != ZX_OK) {
      return zx::error(status);
    }
    // The driver framework now owns driver.
    return zx::ok(driver.release());
  }

  void DdkRelease() { delete this; }
  void DdkInit(ddk::InitTxn txn) {
    txn.Reply(ZX_OK);
    if (parent_init) {
      parent_init->Reply(ZX_OK);
      parent_init.reset();
    }
  }
  std::optional<ddk::InitTxn> parent_init;
};

class InitTestParent;
using DeviceType = ddk::Device<InitTestParent, ddk::Initializable>;
class InitTestParent : public DeviceType {
 public:
  explicit InitTestParent(zx_device_t* root) : DeviceType(root) {}
  virtual ~InitTestParent() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev) {
    auto driver = std::make_unique<InitTestParent>(dev);
    zx_status_t status = driver->Bind();
    if (status != ZX_OK) {
      return status;
    }
    // The driver framework now owns driver.
    __UNUSED auto ptr = driver.release();
    return ZX_OK;
  }

  zx_status_t Bind() {
    return DdkAdd(ddk::DeviceAddArgs("root").set_flags(DEVICE_ADD_NON_BINDABLE));
  }

  void DdkInit(ddk::InitTxn txn) {
    auto child = InitTestChild::Create(zxdev(), "child");
    if (child.is_error()) {
      txn.Reply(child.error_value());
    }
    (*child)->parent_init = std::move(txn);
  }

  void DdkRelease() { delete this; }
};

static zx_driver_ops_t root_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = InitTestParent::Bind;
  return ops;
}();

}  // namespace init_test

ZIRCON_DRIVER(InitTest, init_test::root_driver_ops, "zircon", "0.1");
