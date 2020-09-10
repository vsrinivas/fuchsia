// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/alloc_checker.h>

#include "test-metadata.h"

class TestCompatibilityHookDriver;
using DeviceType = ddk::Device<TestCompatibilityHookDriver, ddk::Initializable,
                               ddk::Unbindable>;
class TestCompatibilityHookDriver : public DeviceType,
                                    public ddk::EmptyProtocol<ZX_PROTOCOL_TEST_COMPAT_CHILD> {
 public:
  TestCompatibilityHookDriver(zx_device_t* parent) : DeviceType(parent) {}
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

 private:
  struct compatibility_test_metadata metadata_;
  size_t metadata_size_;
};

void TestCompatibilityHookDriver::DdkInit(ddk::InitTxn txn) {
  zx_status_t status = DdkAddMetadata(DEVICE_METADATA_PRIVATE, &metadata_, metadata_size_);
  txn.Reply(status);
}

zx_status_t TestCompatibilityHookDriver::Bind() {
  size_t size;
  zx_status_t status = DdkGetMetadataSize(DEVICE_METADATA_TEST, &size);
  if (status != ZX_OK) {
    return status;
  }

  if (size != sizeof(struct compatibility_test_metadata)) {
    printf("Did not get the metadata correctly. size is %lu\n", size);
    return ZX_ERR_INTERNAL;
  }

  status = DdkGetMetadata(DEVICE_METADATA_TEST, &metadata_, size, &metadata_size_);
  if (status != ZX_OK) {
    return status;
  }

  DdkAdd("compatibility-test");
  return ZX_OK;
}

zx_status_t test_compatibility_hook_bind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<TestCompatibilityHookDriver>(&ac, device);
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

static zx_driver_ops_t test_compatibility_hook_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = test_compatibility_hook_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(TestCompatibilityHook, test_compatibility_hook_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_COMPATIBILITY_TEST),
ZIRCON_DRIVER_END(TestCompatibilityHook)
    // clang-format on
