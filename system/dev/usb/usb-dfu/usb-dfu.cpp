// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-dfu.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/usb.h>
#include <fbl/algorithm.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/mem/c/fidl.h>
#include <lib/zx/vmo.h>
#include <zircon/hw/usb-dfu.h>
#include <zircon/usb/test/fwloader/c/fidl.h>

namespace {

inline uint8_t MSB(int n) { return static_cast<uint8_t>(n >> 8); }
inline uint8_t LSB(int n) { return static_cast<uint8_t>(n & 0xFF); }

zx_status_t fidl_LoadPrebuiltFirmware(void* ctx, zircon_usb_test_fwloader_PrebuiltType type,
                                      fidl_txn_t* txn) {
    // TODO(jocelyndang): implement this.
    return zircon_usb_test_fwloader_DeviceLoadPrebuiltFirmware_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

zx_status_t fidl_LoadFirmware(void* ctx, const fuchsia_mem_Buffer* firmware, fidl_txn_t* txn) {
    // TODO(jocelyndang): implement this.
    return zircon_usb_test_fwloader_DeviceLoadFirmware_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

zircon_usb_test_fwloader_Device_ops_t fidl_ops = {
    .LoadPrebuiltFirmware = fidl_LoadPrebuiltFirmware,
    .LoadFirmware = fidl_LoadFirmware,
};

}  // namespace

namespace usb {

zx_status_t Dfu::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return zircon_usb_test_fwloader_Device_dispatch(this, txn, msg, &fidl_ops);
}

zx_status_t Dfu::Bind() {
    zxlogf(TRACE, "adding DFU, interface %x, v%x.%x\n",
           intf_num_, MSB(func_desc_.bcdDFUVersion), LSB(func_desc_.bcdDFUVersion));
    zx_status_t status = DdkAdd("usb-dfu", DEVICE_ADD_NON_BINDABLE);
    if (status != ZX_OK) {
       return status;
    }
    return ZX_OK;
}

// static
zx_status_t Dfu::Create(zx_device_t* parent) {
    usb_protocol_t usb;
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_USB, &usb);
    if (status != ZX_OK) {
        return status;
    }
    usb_desc_iter_t iter;
    status = usb_desc_iter_init(&usb, &iter);
    if (status != ZX_OK) {
        return status;
    }
    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
    if (!intf) {
        usb_desc_iter_release(&iter);
        return ZX_ERR_NOT_SUPPORTED;
    }
    uint8_t intf_num = intf->bInterfaceNumber;

    // Look for the DFU Functional Descriptor.
    usb_dfu_func_desc_t func_desc = {};
    usb_descriptor_header_t* header = usb_desc_iter_next(&iter);
    while (header) {
        if (header->bDescriptorType == USB_DFU_CS_FUNCTIONAL) {
            if (header->bLength < sizeof(func_desc)) {
                zxlogf(ERROR, "DFU func desc should be at least %lu long, got %u\n",
                       sizeof(func_desc), header->bLength);
            } else {
                memcpy(&func_desc, header, sizeof(func_desc));
                zxlogf(TRACE, "DFU func desc bmAttributes %u wDetachTimeOut %u wTransferSize %u\n",
                       func_desc.bmAttributes, func_desc.wDetachTimeOut, func_desc.wTransferSize);
                break;
            }
        }
        header = usb_desc_iter_next(&iter);
    }
    usb_desc_iter_release(&iter);

    if (func_desc.bLength == 0) {
        zxlogf(ERROR, "could not find any valid DFU functional descriptor\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<Dfu>(&ac, parent, intf_num, func_desc);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    status = dev->Bind();
    if (status == ZX_OK) {
        // Intentionally leak as it is now held by DevMgr.
        __UNUSED auto ptr = dev.release();
    }
    return status;
}

zx_status_t dfu_bind(void* ctx, zx_device_t* parent) {
    zxlogf(TRACE, "dfu_bind\n");
    return usb::Dfu::Create(parent);
}

static constexpr zx_driver_ops_t dfu_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = dfu_bind;
    return ops;
}();

}  // namespace usb

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_dfu, usb::dfu_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_APPLICATION_SPECIFIC),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, USB_SUBCLASS_DFU),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, USB_PROTOCOL_DFU),
ZIRCON_DRIVER_END(usb_dfu)
// clang-format on
