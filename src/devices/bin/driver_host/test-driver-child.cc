// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/platform-defs.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddktl/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>

#include "src/devices/bin/driver_host/test-devhost-child-bind.h"
#include "src/devices/bin/driver_host/test-metadata.h"

class TestDevhostDriverChild;
using DeviceType = ddk::Device<TestDevhostDriverChild, ddk::Unbindable, ddk::Initializable>;
class TestDevhostDriverChild : public DeviceType {
 public:
  TestDevhostDriverChild(zx_device_t* parent) : DeviceType(parent) {}
  static zx_status_t Create(void* ctx, zx_device_t* device);
  zx_status_t Bind();
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }
  void DdkInit(ddk::InitTxn txn);

 private:
  struct devhost_test_metadata test_metadata_;
};

zx_status_t TestDevhostDriverChild::Bind() {
  size_t actual;
  auto status =
      DdkGetMetadata(DEVICE_METADATA_PRIVATE, &test_metadata_, sizeof(test_metadata_), &actual);
  if (status != ZX_OK || actual != sizeof(test_metadata_)) {
    zxlogf(ERROR, "TestDevhostDriverChild::Bind: Unable to get metadata correctly");
    return ZX_ERR_INTERNAL;
  }
  return DdkAdd("devhost-test-child");
}

zx_status_t TestDevhostDriverChild::Create(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<TestDevhostDriverChild>(&ac, device);

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

void TestDevhostDriverChild::DdkInit(ddk::InitTxn txn) {
  if (test_metadata_.init_reply_success) {
    txn.Reply(ZX_OK, nullptr);
  } else {
    txn.Reply(ZX_ERR_IO, nullptr);
  }
}

static zx_driver_ops_t test_devhost_child_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TestDevhostDriverChild::Create;
  return ops;
}();

ZIRCON_DRIVER(test - devhost - child, test_devhost_child_driver_ops, "zircon", "0.1");
