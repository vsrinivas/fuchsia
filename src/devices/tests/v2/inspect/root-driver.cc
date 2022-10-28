// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.inspect.test/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>

#include "src/devices/tests/v2/inspect/root-bind.h"

namespace {

class RootDriver;
using DeviceType =
    ddk::Device<RootDriver, ddk::Messageable<fuchsia_inspect_test::Handshake>::Mixin>;
class RootDriver : public DeviceType {
 public:
  explicit RootDriver(zx_device_t* root) : DeviceType(root) {}
  virtual ~RootDriver() = default;

  void DdkRelease() { delete this; }

  static zx_status_t Bind(void* ctx, zx_device_t* dev) {
    auto driver = std::make_unique<RootDriver>(dev);
    zx_status_t status = driver->Bind();
    if (status != ZX_OK) {
      return status;
    }
    // The DriverFramework now owns driver.
    __UNUSED auto ptr = driver.release();
    return ZX_OK;
  }

  zx_status_t Bind() {
    root_ = inspect_.GetRoot().CreateChild("connection_info");
    request_count_property_ = root_.CreateUint("request_count", 0);
    return DdkAdd(ddk::DeviceAddArgs("root-driver").set_inspect_vmo(inspect_vmo()));
  }

  // fuchsia_inspect_test::Handshake
  void Do(DoCompleter::Sync& completer) override {
    request_count_property_.Add(1);
    completer.Reply();
  }

 private:
  zx::vmo inspect_vmo() { return inspect_.DuplicateVmo(); }

  inspect::Inspector inspect_;
  inspect::Node root_;
  inspect::UintProperty request_count_property_;
};

static zx_driver_ops_t root_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = RootDriver::Bind;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(RootDriver, root_driver_ops, "zircon", "0.1");
