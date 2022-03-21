// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-driver-child.h"

#include <lib/ddk/debug.h>

#include <ddktl/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

namespace {
uint32_t device_index = 0;
}  // namespace

void TestLifecycleDriverChild::DdkRelease() {
  // Release the reference now that devmgr no longer has a pointer to this object.
  if (Release()) {
    delete this;
  }
}

void TestLifecycleDriverChild::AsyncRemove(fit::function<void()> callback) {
  async_remove_ = true;
  unbind_callback_ = std::move(callback);
  DdkAsyncRemove();
}

void TestLifecycleDriverChild::CompleteUnbind() { unbind_txn_->Reply(); }

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

  std::string name = "ddk-lifecycle-test-child-" + std::to_string(device_index++);
  zx_status_t status =
      device->DdkAdd(ddk::DeviceAddArgs(name.data()).set_flags(DEVICE_ADD_NON_BINDABLE));
  if (status != ZX_OK) {
    out_device->reset();
    return status;
  }
  // Hold a reference while devmgr has a pointer to this object.
  device->AddRef();
  return ZX_OK;
}

zx_status_t TestLifecycleDriverChild::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
  return (new InstanceDevice(zxdev()))->Add(dev_out);
}

void TestLifecycleDriverChild::DdkInit(ddk::InitTxn txn) {
  if (complete_init_) {
    txn.Reply(init_status_);
    replied_to_init_ = true;
  } else {
    init_txn_ = std::move(txn);
  }
}

void TestLifecycleDriverChild::DdkUnbind(ddk::UnbindTxn txn) {
  ZX_ASSERT(!init_txn_);
  if (async_remove_) {
    unbind_txn_ = std::move(txn);
    (*unbind_callback_)();
    return;
  }
  txn.Reply();
}

zx_status_t TestLifecycleDriverChild::CompleteInit() {
  if (replied_to_init_) {
    zxlogf(ERROR, "Already replied to init");
    return ZX_ERR_BAD_STATE;
  }
  if (!init_txn_) {
    // The init hook has not been called yet.
    complete_init_ = true;
    return ZX_OK;
  }
  init_txn_->Reply(init_status_);
  init_txn_ = std::nullopt;
  replied_to_init_ = true;
  return ZX_OK;
}
