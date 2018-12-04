// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flash-programmer.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/unique_ptr.h>
#include <zircon/usb/test/fwloader/c/fidl.h>

#include "flash-programmer-hw.h"

namespace {

zx_status_t fidl_LoadPrebuiltFirmware(void* ctx, fidl_txn_t* txn) {
    // TODO(jocelyndang): implement this.
    return zircon_usb_test_fwloader_DeviceLoadPrebuiltFirmware_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

zx_status_t fidl_LoadFirmware(void* ctx, zx_handle_t fw_vmo, fidl_txn_t* txn) {
    // TODO(jocelyndang): implement this.
    return zircon_usb_test_fwloader_DeviceLoadFirmware_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

zircon_usb_test_fwloader_Device_ops_t fidl_ops = {
    .LoadPrebuiltFirmware = fidl_LoadPrebuiltFirmware,
    .LoadFirmware = fidl_LoadFirmware,
};

}  // namespace

namespace usb {

zx_status_t FlashProgrammer::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return zircon_usb_test_fwloader_Device_dispatch(this, txn, msg, &fidl_ops);
}

zx_status_t FlashProgrammer::Bind() {
    return DdkAdd("flash-programmer", DEVICE_ADD_NON_BINDABLE);
}

// static
zx_status_t FlashProgrammer::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<FlashProgrammer> dev(new (&ac) FlashProgrammer(parent));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = dev->Bind();
    if (status == ZX_OK) {
        // Intentionally leak as it is now held by DevMgr.
        __UNUSED auto ptr = dev.release();
    }
    return status;
}

extern "C" zx_status_t flash_programmer_bind(void* ctx, zx_device_t* parent) {
    zxlogf(TRACE, "flash_programmer_bind\n");
    return usb::FlashProgrammer::Create(parent);
}

static zx_driver_ops_t flash_programmer_driver_ops = []() {
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = flash_programmer_bind;
    return ops;
}();

}  // namespace usb

// clang-format off
ZIRCON_DRIVER_BEGIN(flash_programmer, usb::flash_programmer_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_DEVICE),
    BI_ABORT_IF(NE, BIND_USB_VID, CYPRESS_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, FLASH_PROGRAMMER_PID),
ZIRCON_DRIVER_END(flash_programmer)
// clang-format on
