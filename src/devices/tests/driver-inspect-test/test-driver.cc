// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/inspect/test/llcpp/fidl.h>
#include <lib/inspect/cpp/inspect.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/alloc_checker.h>

#include "src/devices/tests/driver-inspect-test/inspect-test-bind.h"

namespace {

using llcpp::fuchsia::device::inspect::test::TestInspect;

class TestInspectDriver;
using DeviceType = ddk::Device<TestInspectDriver, ddk::Messageable>;
class TestInspectDriver : public DeviceType,
                          public ddk::EmptyProtocol<ZX_PROTOCOL_TEST>,
                          public TestInspect::Interface {
 public:
  TestInspectDriver(zx_device_t* parent) : DeviceType(parent) {}
  zx_status_t Bind();

  // Device protocol ops implementation.
  void DdkRelease() { delete this; }
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    TestInspect::Dispatch(this, msg, &transaction);
    return transaction.Status();
  }

  // Device message ops implementation.
  void ModifyInspect(ModifyInspectCompleter::Sync& completer) override;

  zx::vmo inspect_vmo() { return inspect_.DuplicateVmo(); }

 private:
  inspect::Inspector inspect_;
};

void TestInspectDriver::ModifyInspect(ModifyInspectCompleter::Sync& completer) {
  inspect_.GetRoot().CreateString("testModify", "OK", &inspect_);
  completer.ReplySuccess();
}

zx_status_t TestInspectDriver::Bind() {
  inspect_.GetRoot().CreateString("testBeforeDdkAdd", "OK", &inspect_);
  return DdkAdd(ddk::DeviceAddArgs("inspect-test").set_inspect_vmo(inspect_vmo()));
}

zx_status_t test_inspect_bind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<TestInspectDriver>(&ac, device);
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

zx_driver_ops_t test_inspect_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = test_inspect_bind;
  return ops;
}();
}  // namespace

ZIRCON_DRIVER(TestInspect, test_inspect_driver_ops, "zircon", "0.1");
