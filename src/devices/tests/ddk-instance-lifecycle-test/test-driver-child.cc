// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-driver-child.h"

#include <lib/ddk/debug.h>

#include <ddktl/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

using fuchsia_device_instancelifecycle_test::Lifecycle;

void TestLifecycleDriverChild::DdkRelease() {
  fidl::Result result = lifecycle_.OnRelease();
  ZX_ASSERT_MSG(result.ok(), "%s", result.FormatDescription().c_str());
  delete this;
}

zx_status_t TestLifecycleDriverChild::Create(zx_device_t* parent,
                                             fidl::ServerEnd<Lifecycle> lifecycle_client,
                                             zx::channel instance_client) {
  auto device = std::make_unique<TestLifecycleDriverChild>(parent, std::move(lifecycle_client));

  zx_status_t status = device->DdkAdd(ddk::DeviceAddArgs("child")
                                          .set_flags(DEVICE_ADD_NON_BINDABLE)
                                          .set_client_remote(std::move(instance_client)));
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = device.release();
  }
  return status;
}

void TestLifecycleDriverChild::DdkUnbind(ddk::UnbindTxn txn) {
  fidl::Result result = lifecycle_.OnUnbind();
  ZX_ASSERT_MSG(result.ok(), "%s", result.FormatDescription().c_str());
  txn.Reply();
}

zx_status_t TestLifecycleDriverChild::DdkOpen(zx_device_t** out, uint32_t flags) {
  fidl::Result result = lifecycle_.OnOpen();
  ZX_ASSERT_MSG(result.ok(), "%s", result.FormatDescription().c_str());

  auto device = std::make_unique<TestLifecycleDriverChildInstance>(zxdev(), this);
  zx_status_t status = device->DdkAdd("child-instance", DEVICE_ADD_INSTANCE);
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
  if (lifecycle_.is_valid()) {
    fidl::Result result = lifecycle_.OnClose();
    ZX_ASSERT_MSG(result.ok(), "%s", result.FormatDescription().c_str());
  }
  return ZX_OK;
}

void TestLifecycleDriverChildInstance::DdkRelease() {
  if (lifecycle_.is_valid()) {
    fidl::Result result = lifecycle_.OnRelease();
    ZX_ASSERT_MSG(result.ok(), "%s", result.FormatDescription().c_str());
  }
  delete this;
}

void TestLifecycleDriverChildInstance::RemoveDevice(RemoveDeviceRequestView request,
                                                    RemoveDeviceCompleter::Sync& completer) {
  parent_ctx_->DdkAsyncRemove();
}

void TestLifecycleDriverChildInstance::SubscribeToLifecycle(
    SubscribeToLifecycleRequestView request, SubscribeToLifecycleCompleter::Sync& completer) {
  // Currently we only care about supporting one client.
  if (lifecycle_.is_valid()) {
    completer.ReplyError(ZX_ERR_ALREADY_BOUND);
  } else {
    lifecycle_ = fidl::WireEventSender<Lifecycle>(std::move(request->lifecycle));
    completer.ReplySuccess();
  }
}
