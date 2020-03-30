// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-driver-child.h"

#include <ddk/debug.h>
#include <ddktl/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

using llcpp::fuchsia::device::instancelifecycle::test::Lifecycle;

void TestLifecycleDriverChild::DdkRelease() {
  zx_status_t status = Lifecycle::SendOnReleaseEvent(zx::unowned(lifecycle_));
  ZX_ASSERT(status == ZX_OK);
  delete this;
}

zx_status_t TestLifecycleDriverChild::Create(zx_device_t* parent, zx::channel lifecycle_client,
                                             zx::channel instance_client) {
  auto device = std::make_unique<TestLifecycleDriverChild>(parent, std::move(lifecycle_client));

  zx_status_t status = device->DdkAdd("child", /* flags */ 0, /* props */ nullptr,
                                      /* prop_count */ 0, /* proto_id */ 0,
                                      /* proxy_args */ nullptr, instance_client.release());
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = device.release();
  }
  return status;
}

void TestLifecycleDriverChild::DdkUnbindNew(ddk::UnbindTxn txn) {
  zx_status_t status = Lifecycle::SendOnUnbindEvent(zx::unowned(lifecycle_));
  ZX_ASSERT(status == ZX_OK);
  txn.Reply();
}

zx_status_t TestLifecycleDriverChild::DdkOpen(zx_device_t** out, uint32_t flags) {
  zx_status_t status = Lifecycle::SendOnOpenEvent(zx::unowned(lifecycle_));
  ZX_ASSERT(status == ZX_OK);

  auto device = std::make_unique<TestLifecycleDriverChildInstance>(zxdev(), this);
  status = device->DdkAdd("child-instance", DEVICE_ADD_INSTANCE);
  if (status != ZX_OK) {
    return status;
  }

  *out = device->zxdev();
  // devmgr is now in charge of the memory for dev
  __UNUSED auto ptr = device.release();
  return ZX_OK;
}

// Implementation of the instance devices

zx_status_t TestLifecycleDriverChildInstance::DdkClose(uint32_t flags) {
  if (lifecycle_) {
    zx_status_t status = Lifecycle::SendOnCloseEvent(zx::unowned(lifecycle_));
    ZX_ASSERT(status == ZX_OK);
  }
  return ZX_OK;
}

void TestLifecycleDriverChildInstance::DdkRelease() {
  if (lifecycle_) {
    zx_status_t status = Lifecycle::SendOnReleaseEvent(zx::unowned(lifecycle_));
    ZX_ASSERT(status == ZX_OK);
  }
  delete this;
}

void TestLifecycleDriverChildInstance::RemoveDevice(RemoveDeviceCompleter::Sync completer) {
  parent_ctx_->DdkAsyncRemove();
}

void TestLifecycleDriverChildInstance::SubscribeToLifecycle(
    zx::channel client, SubscribeToLifecycleCompleter::Sync completer) {
  // Currently we only care about supporting one client.
  if (lifecycle_) {
    completer.ReplyError(ZX_ERR_ALREADY_BOUND);
  } else {
    lifecycle_ = std::move(client);
    completer.ReplySuccess();
  }
}
