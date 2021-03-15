// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/manager/test/c/fidl.h>
#include <lib/ddk/platform-defs.h>

#include <vector>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <fbl/alloc_checker.h>

#include "src/devices/tests/isolateddevmgr/metadata-test-bind.h"

class IsolatedDevMgrTestDriver;
using DeviceType = ddk::Device<IsolatedDevMgrTestDriver, ddk::Unbindable, ddk::Messageable>;
class IsolatedDevMgrTestDriver : public DeviceType {
 public:
  IsolatedDevMgrTestDriver(zx_device_t* parent) : DeviceType(parent) {}
  zx_status_t Bind();
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);

 private:
  static zx_status_t FidlGetMetadata(void* ctx, uint32_t type, fidl_txn_t* txn);
};

zx_status_t IsolatedDevMgrTestDriver::FidlGetMetadata(void* ctx, uint32_t type, fidl_txn_t* txn) {
  IsolatedDevMgrTestDriver* driver = reinterpret_cast<IsolatedDevMgrTestDriver*>(ctx);
  size_t size;
  zx_status_t status = driver->DdkGetMetadataSize(type, &size);
  if (status != ZX_OK) {
    return status;
  }

  std::vector<uint8_t> metadata;
  metadata.resize(size);

  status = driver->DdkGetMetadata(type, metadata.data(), metadata.size(), &size);
  if (status != ZX_OK) {
    return status;
  }
  if (size != metadata.size()) {
    return ZX_ERR_INTERNAL;
  }

  return fuchsia_device_manager_test_MetadataGetMetadata_reply(txn, metadata.data(),
                                                               metadata.size());
}

zx_status_t IsolatedDevMgrTestDriver::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  static const fuchsia_device_manager_test_Metadata_ops_t kOps = {
      .GetMetadata = IsolatedDevMgrTestDriver::FidlGetMetadata,
  };
  return fuchsia_device_manager_test_Metadata_dispatch(this, txn, msg, &kOps);
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
