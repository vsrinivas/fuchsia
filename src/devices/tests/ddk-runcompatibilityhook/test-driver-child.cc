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
#include <fbl/alloc_checker.h>

#include "test-metadata.h"

class TestCompatibilityHookDriverChild;
using DeviceType = ddk::Device<TestCompatibilityHookDriverChild, ddk::UnbindableDeprecated>;
class TestCompatibilityHookDriverChild : public DeviceType {
 public:
  TestCompatibilityHookDriverChild(zx_device_t* parent) : DeviceType(parent) {}
  static zx_status_t Create(void* ctx, zx_device_t* device);
  zx_status_t Bind();
  void DdkUnbindDeprecated() {
    if (test_metadata_.remove_in_unbind) {
      DdkRemoveDeprecated();
    }
  }
  void DdkRelease() { delete this; }
  struct compatibility_test_metadata test_metadata_ = {};
};

zx_status_t TestCompatibilityHookDriverChild::Bind() {
  size_t actual;
  auto status =
      DdkGetMetadata(DEVICE_METADATA_PRIVATE, &test_metadata_, sizeof(test_metadata_), &actual);
  if (status != ZX_OK || actual != sizeof(test_metadata_)) {
    zxlogf(ERROR, "test_compat_hook_child_get_metadata not succesful");
    return ZX_ERR_INTERNAL;
  }
  if (test_metadata_.add_in_bind) {
    return DdkAdd("compatibility-test-child");
  }
  return ZX_OK;
}

zx_status_t TestCompatibilityHookDriverChild::Create(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<TestCompatibilityHookDriverChild>(&ac, device);
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

static zx_driver_ops_t test_compatibility_hook_child_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TestCompatibilityHookDriverChild::Create;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(TestCompatibilityHookChild, test_compatibility_hook_child_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_COMPAT_CHILD),
ZIRCON_DRIVER_END(TestCompatibilityHookChild)
    // clang-format on
