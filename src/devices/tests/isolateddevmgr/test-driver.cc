// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/manager/test/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>

#include <vector>

#include <ddktl/device.h>
#include <fbl/alloc_checker.h>

#include "src/devices/tests/isolateddevmgr/metadata-test-bind.h"

class IsolatedDevMgrTestDriver;
using DeviceType = ddk::Device<IsolatedDevMgrTestDriver, ddk::Unbindable,
                               ddk::Messageable<fuchsia_device_manager_test::Metadata>::Mixin>;
class IsolatedDevMgrTestDriver : public DeviceType {
 public:
  IsolatedDevMgrTestDriver(zx_device_t* parent) : DeviceType(parent) {}
  zx_status_t Bind();
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  void GetMetadata(GetMetadataRequestView request, GetMetadataCompleter::Sync& completer);
};

void IsolatedDevMgrTestDriver::GetMetadata(GetMetadataRequestView request,
                                           GetMetadataCompleter::Sync& completer) {
  size_t size;
  zx_status_t status = DdkGetMetadataSize(request->type, &size);
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }

  std::vector<uint8_t> metadata;
  metadata.resize(size);

  status = DdkGetMetadata(request->type, metadata.data(), metadata.size(), &size);
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }
  if (size != metadata.size()) {
    completer.Close(ZX_ERR_INTERNAL);
    return;
  }

  completer.Reply(fidl::VectorView<uint8_t>::FromExternal(metadata));
}

zx_status_t IsolatedDevMgrTestDriver::Bind() { return DdkAdd("metadata-test"); }

zx_status_t isolateddevmgr_test_bind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<IsolatedDevMgrTestDriver>(&ac, device);
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

static constexpr zx_driver_ops_t isolateddevmgr_test_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = isolateddevmgr_test_bind;
  return ops;
}();

ZIRCON_DRIVER(metadata - test, isolateddevmgr_test_driver_ops, "zircon", "0.1");
