// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/driver/test/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/alloc_checker.h>

#include "src/devices/tests/device-watcher/test-driver-bind.h"

class TestDriver;
using DeviceType =
    ddk::Device<TestDriver, ddk::Unbindable, ddk::Messageable<fuchsia_driver_test::Logger>::Mixin>;

class TestDriver : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_USB_DEVICE> {
 public:
  explicit TestDriver(zx_device_t* parent) : DeviceType(parent) {}

  zx_status_t Bind() { return DdkAdd("test-driver"); }

  // Device protocol implementation.
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  void LogMessage(LogMessageRequestView request, LogMessageCompleter::Sync& completer) override {}

  void LogTestCase(LogTestCaseRequestView request, LogTestCaseCompleter::Sync& completer) override {
  }

 private:
};

zx_status_t Bind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<TestDriver>(&ac, device);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

zx_driver_ops_t driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Bind;
  return ops;
}();

ZIRCON_DRIVER(TestDriver, driver_ops, "zircon", "0.1");
