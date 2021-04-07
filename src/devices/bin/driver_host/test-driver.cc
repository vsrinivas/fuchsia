// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/restarttest/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/alloc_checker.h>

#include "src/devices/bin/driver_host/test-devhost-parent-bind.h"
#include "src/devices/bin/driver_host/test-metadata.h"

using fuchsia_device_restarttest::TestDevice;

class TestDevhostDriver;
using DeviceType =
    ddk::Device<TestDevhostDriver, ddk::Initializable, ddk::Unbindable, ddk::Messageable>;
class TestDevhostDriver : public DeviceType,
                          public ddk::EmptyProtocol<ZX_PROTOCOL_DEVHOST_TEST>,
                          public fidl::WireInterface<TestDevice> {
 public:
  TestDevhostDriver(zx_device_t* parent) : DeviceType(parent) {}
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  // Device message ops implementation.

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    fidl::WireDispatch<TestDevice>(this, msg, &transaction);
    return transaction.Status();
  }

 private:
  struct devhost_test_metadata metadata_;
  size_t metadata_size_;

  void GetPid(GetPidCompleter::Sync& _completer) override;
};

void TestDevhostDriver::GetPid(GetPidCompleter::Sync& _completer) {
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

zx_status_t TestDevhostDriver::Bind() {
  size_t size;
  zx_status_t status = DdkGetMetadataSize(DEVICE_METADATA_TEST, &size);
  if (status != ZX_OK) {
    return status;
  }

  if (size != sizeof(struct devhost_test_metadata)) {
    printf("Unable to get the metadata correctly. size is %lu\n", size);
    return ZX_ERR_INTERNAL;
  }

  status = DdkGetMetadata(DEVICE_METADATA_TEST, &metadata_, size, &metadata_size_);
  if (status != ZX_OK) {
    printf("Unable to get the metadata. size is %lu\n", size);
    return status;
  }

  return DdkAdd("devhost-test-parent");
}

void TestDevhostDriver::DdkInit(ddk::InitTxn txn) {
  zx_status_t status = DdkAddMetadata(DEVICE_METADATA_PRIVATE, &metadata_, metadata_size_);
  txn.Reply(status);
}

zx_status_t TestDevhostDriverBind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<TestDevhostDriver>(&ac, device);
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

static zx_driver_ops_t test_devhost_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TestDevhostDriverBind;
  return ops;
}();

ZIRCON_DRIVER(test - devhost - parent, test_devhost_driver_ops, "zircon", "0.1");
