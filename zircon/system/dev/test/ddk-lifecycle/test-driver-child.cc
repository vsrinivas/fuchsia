// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-driver-child.h"

#include <ddktl/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

void TestLifecycleDriverChild::DdkRelease() {
  // Release the reference now that devmgr no longer has a pointer to this object.
  __UNUSED bool dummy = Release();
}

zx_status_t TestLifecycleDriverChild::Create(zx_device_t* parent,
                                             fbl::RefPtr<TestLifecycleDriverChild>* out_device) {
  fbl::AllocChecker ac;
  auto device = fbl::MakeRefCountedChecked<TestLifecycleDriverChild>(&ac, parent);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  *out_device = device;

  zx_status_t status = device->DdkAdd("ddk-lifecycle-test-child");
  if (status != ZX_OK) {
    out_device->reset();
    return status;
  }
  // Hold a reference while devmgr has a pointer to this object.
  device->AddRef();
  return ZX_OK;
}
