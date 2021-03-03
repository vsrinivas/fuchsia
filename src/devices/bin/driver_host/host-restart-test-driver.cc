// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/restarttest/llcpp/fidl.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>

#include "src/devices/bin/driver_host/driver-host-restart-bind.h"

namespace {

using fuchsia_device_restarttest::TestDevice;

class TestHostRestartDriver;
using DeviceType = ddk::Device<TestHostRestartDriver, ddk::Unbindable, ddk::Messageable>;

class TestHostRestartDriver : public DeviceType, public TestDevice::Interface {
 public:
  explicit TestHostRestartDriver(zx_device_t* parent) : DeviceType(parent) {}

  zx_status_t Bind() { return DdkAdd("driver-host-restart-driver"); }

  // Device protocol implementation.
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  // Device message ops implementation.

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    TestDevice::Dispatch(this, msg, &transaction);
    return transaction.Status();
  }

  void GetPid(GetPidCompleter::Sync& _completer) override;
};

void TestHostRestartDriver::GetPid(GetPidCompleter::Sync& _completer) {
  pid_t pid;
  auto self = zx_process_self();
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(self, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    pid = ZX_KOID_INVALID;
  } else {
    pid = info.koid;
  }
  _completer.ReplySuccess(pid);
}

zx_status_t TestHostRestartBind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<TestHostRestartDriver>(&ac, device);
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
  ops.bind = TestHostRestartBind;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(TestHostRestart, driver_ops, "zircon", "0.1");
