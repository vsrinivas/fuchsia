// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "driver.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/function.h>
#include <fbl/unique_ptr.h>

namespace fidl {

namespace fuchsia = ::llcpp::fuchsia;

zx_status_t DdkFidlDevice::Create(void* ctx, zx_device_t* dev) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<DdkFidlDevice> serial_dev(new (&ac) DdkFidlDevice(dev));

    if (!ac.check()) {
        zxlogf(ERROR, "DdkFidlDevice::Create: no memory to allocate device!\n");
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status;
    if ((status = serial_dev->Bind()) != ZX_OK) {
        zxlogf(ERROR, "DdkFidlDevice::Create: Bind failed\n");
        serial_dev.release()->DdkRelease();
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = serial_dev.release();

    return ZX_OK;
}

zx_status_t DdkFidlDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    fuchsia::hardware::serial::Device::Dispatch(this, msg, &transaction);
    return transaction.Status();
}

void DdkFidlDevice::GetClass(GetClassCompleter::Sync completer) {
    completer.Reply(fuchsia::hardware::serial::Class::CONSOLE);
}

void DdkFidlDevice::SetConfig(fuchsia::hardware::serial::Config config,
                              SetConfigCompleter::Sync completer) {}

zx_status_t DdkFidlDevice::Bind() {
    return DdkAdd("ddk-fidl");
}

void DdkFidlDevice::DdkRelease() {
    delete this;
}

constexpr zx_driver_ops_t driver_ops = []() {
    zx_driver_ops_t driver_ops = {};
    driver_ops.version = DRIVER_OPS_VERSION;
    driver_ops.bind = DdkFidlDevice::Create;
    return driver_ops;
}();

} // namespace fidl

// clang-format off
ZIRCON_DRIVER_BEGIN(ddk_fidl, fidl::driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_DDKFIDL_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_DDKFIDL),
ZIRCON_DRIVER_END(ddk_fidl)
// clang-format on
