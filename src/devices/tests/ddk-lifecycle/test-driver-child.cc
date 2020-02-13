// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-driver-child.h"

#include <ddk/debug.h>
#include <ddktl/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

void TestLifecycleDriverChild::DdkRelease() {
  // Release the reference now that devmgr no longer has a pointer to this object.
  __UNUSED bool dummy = Release();
}

zx_status_t TestLifecycleDriverChild::Create(zx_device_t* parent, bool complete_init,
                                             zx_status_t init_status,
                                             fbl::RefPtr<TestLifecycleDriverChild>* out_device) {
  fbl::AllocChecker ac;
  auto device =
      fbl::MakeRefCountedChecked<TestLifecycleDriverChild>(&ac, parent, complete_init, init_status);
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

void TestLifecycleDriverChild::DdkInit(ddk::InitTxn txn) {
  if (complete_init_) {
    txn.Reply(init_status_);
  } else {
    init_txn_ = std::move(txn);
  }
}

void TestLifecycleDriverChild::DdkUnbindNew(ddk::UnbindTxn txn) {
  ZX_ASSERT(!init_txn_);
  txn.Reply();
}

zx_status_t TestLifecycleDriverChild::CompleteInit() {
  if (!init_txn_) {
    zxlogf(ERROR, "Child does not have a pending init txn");
    return ZX_ERR_BAD_STATE;
  }
  init_txn_->Reply(init_status_);
  init_txn_ = std::nullopt;
  return ZX_OK;
}
