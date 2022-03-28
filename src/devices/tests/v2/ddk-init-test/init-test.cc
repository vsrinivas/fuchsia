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

#include "src/devices/tests/v2/ddk-init-test/init-test-bind.h"

namespace init_test {

class InitTestChild;
using ChildDeviceType = ddk::Device<InitTestChild, ddk::Initializable>;
class InitTestChild : public ChildDeviceType {
 public:
  explicit InitTestChild(zx_device_t* parent) : ChildDeviceType(parent) {}
  static zx_status_t Create(zx_device_t* parent) {
    auto driver = std::make_unique<InitTestChild>(parent);
    zx_status_t status = driver->DdkAdd(ddk::DeviceAddArgs("child"));
    if (status == ZX_OK) {
      // The driver framework now owns driver.
      __UNUSED auto ptr = driver.release();
    }
    return status;
  }

  void DdkRelease() { delete this; }
  void DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }
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
    zx_status_t result = DdkAdd(ddk::DeviceAddArgs("root").set_flags(DEVICE_ADD_NON_BINDABLE));
    if (result != ZX_OK) {
      return result;
    }

    add_child_thread_ = std::thread([this]() {
      // Wait until the dispatcher thread init() would get called on is idle, hopefully.
      // That way we'll trigger races between device_add() and init().
      sleep(5);
      InitTestChild::Create(zxdev());
    });
    return ZX_OK;
  }

  void DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

  void DdkRelease() {
    add_child_thread_.join();
    delete this;
  }

 private:
  std::thread add_child_thread_;
};

static zx_driver_ops_t root_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = InitTestParent::Bind;
  return ops;
}();

}  // namespace init_test

ZIRCON_DRIVER(InitTest, init_test::root_driver_ops, "zircon", "0.1");
