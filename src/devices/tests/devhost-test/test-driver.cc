// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/devhost/test/llcpp/fidl.h>
#include <zircon/threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/alloc_checker.h>

#include "test-metadata.h"

using llcpp::fuchsia::device::devhost::test::TestDevice;

class TestDevhostDriver;
using DeviceType =
    ddk::Device<TestDevhostDriver, ddk::Initializable, ddk::Unbindable, ddk::Messageable>;
class TestDevhostDriver : public DeviceType,
                          public ddk::EmptyProtocol<ZX_PROTOCOL_DEVHOST_TEST>,
                          public TestDevice::Interface {
 public:
  TestDevhostDriver(zx_device_t* parent) : DeviceType(parent) {}
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }
  void AddChildDevice(AddChildDeviceCompleter::Sync completer) override;

  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    ::llcpp::fuchsia::device::devhost::test::TestDevice::Dispatch(this, msg, &transaction);
    return transaction.Status();
  }

 private:
  struct devhost_test_metadata metadata_;
  size_t metadata_size_;
};

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

void TestDevhostDriver::AddChildDevice(AddChildDeviceCompleter::Sync completer) {
  ::llcpp::fuchsia::device::devhost::test::TestDevice_AddChildDevice_Result response;
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  response.set_err(fidl::unowned_ptr(&status));
  completer.Reply(std::move(response));
}

zx_status_t test_devhost_driver_bind(void* ctx, zx_device_t* device) {
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
  ops.bind = test_devhost_driver_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(test-devhost-parent, test_devhost_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_DEVHOST_TEST),
ZIRCON_DRIVER_END(test-devhost-parent)
    // clang-format on
