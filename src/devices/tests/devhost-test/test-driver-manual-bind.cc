// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/devhost/test/llcpp/fidl.h>
#include <zircon/errors.h>
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

class TestDevhostDriverChild;
using DeviceType = ddk::Device<TestDevhostDriverChild, ddk::UnbindableNew, ddk::Messageable>;
class TestDevhostDriverChild : public DeviceType, public TestDevice::Interface {
 public:
  TestDevhostDriverChild(zx_device_t* parent) : DeviceType(parent) {}
  zx_status_t Bind();
  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }
  void AddChildDevice(AddChildDeviceCompleter::Sync completer) override;
  zx_status_t Init();

  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    ::llcpp::fuchsia::device::devhost::test::TestDevice::Dispatch(this, msg, &transaction);
    return transaction.Status();
  }

 private:
  struct devhost_test_metadata metadata_;
  thrd_t init_thread_;
};

zx_status_t TestDevhostDriverChild::Bind() {
  size_t size;
  zx_status_t status = DdkGetMetadataSize(DEVICE_METADATA_PRIVATE, &size);
  if (status != ZX_OK) {
    return status;
  }

  if (size != sizeof(struct devhost_test_metadata)) {
    printf("Did not get the metadata correctly. size is %lu\n", size);
    return ZX_ERR_INTERNAL;
  }

  status = DdkGetMetadata(DEVICE_METADATA_PRIVATE, &metadata_, size, &size);
  if (status != ZX_OK) {
    return status;
  }

  DdkAdd("devhost-test-child", DEVICE_ADD_INVISIBLE);
  auto f = [](void* arg) -> int { return reinterpret_cast<TestDevhostDriverChild*>(arg)->Init(); };

  int rc = thrd_create_with_name(&init_thread_, f, this, "devhost-test-child-init-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t TestDevhostDriverChild::Init() {
  if (!metadata_.make_device_visible_success) {
    // Fail the makedevice visible. Remove the device.
    DdkAsyncRemove();
    return ZX_OK;
  }
  DdkMakeVisible();
  return ZX_OK;
}

void TestDevhostDriverChild::AddChildDevice(AddChildDeviceCompleter::Sync completer) {
  ::llcpp::fuchsia::device::devhost::test::TestDevice_AddChildDevice_Result response;
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  response.set_err(&status);
  completer.Reply(std::move(response));
}

zx_status_t test_devhost_driver_child_bind(void* ctx, zx_device_t* device) {
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

static zx_driver_ops_t test_devhost_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = test_devhost_driver_child_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(test-devhost-child-manual, test_devhost_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_DEVHOST_TEST),
ZIRCON_DRIVER_END(test-devhost-child-manual)
    // clang-format on
