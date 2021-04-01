// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/ddk-fidl-test/fidl-llcpp-driver.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>

#include <memory>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/function.h>

#include "src/devices/tests/ddk-fidl-test/ddk-fidl-bind.h"

namespace fidl {

zx_status_t DdkFidlDevice::Create(void* ctx, zx_device_t* dev) {
  fbl::AllocChecker ac;
  std::unique_ptr<DdkFidlDevice> test_dev(new (&ac) DdkFidlDevice(dev));

  if (!ac.check()) {
    zxlogf(ERROR, "DdkFidlDevice::Create: no memory to allocate device!");
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status;
  if ((status = test_dev->Bind()) != ZX_OK) {
    zxlogf(ERROR, "DdkFidlDevice::Create: Bind failed");
    test_dev.release()->DdkRelease();
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = test_dev.release();

  return ZX_OK;
}

zx_status_t DdkFidlDevice::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fidl::WireDispatch<fuchsia_hardware_test::Device>(this, msg, &transaction);
  return transaction.Status();
}

void DdkFidlDevice::GetChannel(GetChannelCompleter::Sync& completer) {
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  __UNUSED auto dummy = local.release();
  completer.Reply(std::move(remote));
}

zx_status_t DdkFidlDevice::Bind() { return DdkAdd("ddk-fidl"); }

void DdkFidlDevice::DdkRelease() { delete this; }

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = DdkFidlDevice::Create;
  return driver_ops;
}();

}  // namespace fidl

ZIRCON_DRIVER(ddk_fidl, fidl::driver_ops, "zircon", "0.1");
