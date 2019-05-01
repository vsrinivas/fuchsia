// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdio-function-device.h"

#include <ddk/debug.h>
#include <zircon/driver/binding.h>

#include "sdio-controller-device.h"

namespace sdmmc {

zx_status_t SdioFunctionDevice::Create(zx_device_t* parent, SdioControllerDevice* sdio_parent,
                                       fbl::RefPtr<SdioFunctionDevice>* out_dev) {
    fbl::AllocChecker ac;
    auto dev = fbl::MakeRefCountedChecked<SdioFunctionDevice>(&ac, parent, sdio_parent);
    if (!ac.check()) {
        zxlogf(ERROR, "sdmmc: failed to allocate device memory\n");
        return ZX_ERR_NO_MEMORY;
    }

    *out_dev = dev;
    return ZX_OK;
}

void SdioFunctionDevice::DdkUnbind() {
    if (dead_) {
        return;
    }

    dead_ = true;
    DdkRemove();
}

void SdioFunctionDevice::DdkRelease() {
    dead_ = true;
    __UNUSED bool dummy = Release();
}

zx_status_t SdioFunctionDevice::AddDevice(const sdio_func_hw_info_t& hw_info, uint32_t func) {
    constexpr size_t kNameBufferSize = sizeof("sdmmc-sdio-") + 1;

    zx_device_prop_t props[] = {
        {BIND_SDIO_VID, 0, hw_info.manufacturer_id},
        {BIND_SDIO_PID, 0, hw_info.product_id},
        {BIND_SDIO_FUNCTION, 0, func},
    };

    char name[kNameBufferSize];
    snprintf(name, sizeof(name), "sdmmc-sdio-%u", func);
    zx_status_t st = DdkAdd(name, 0, props, countof(props));
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdmmc: Failed to add sdio device, retcode = %d\n", st);
    }

    return st;
}

zx_status_t SdioFunctionDevice::SdioGetDevHwInfo(sdio_hw_info_t* out_hw_info) {
    return sdio_parent_->SdioGetDevHwInfo(out_hw_info);
}

zx_status_t SdioFunctionDevice::SdioEnableFn(uint8_t fn_idx) {
    return sdio_parent_->SdioEnableFn(fn_idx);
}

zx_status_t SdioFunctionDevice::SdioDisableFn(uint8_t fn_idx) {
    return sdio_parent_->SdioDisableFn(fn_idx);
}

zx_status_t SdioFunctionDevice::SdioEnableFnIntr(uint8_t fn_idx) {
    return sdio_parent_->SdioEnableFnIntr(fn_idx);
}

zx_status_t SdioFunctionDevice::SdioDisableFnIntr(uint8_t fn_idx) {
    return sdio_parent_->SdioDisableFnIntr(fn_idx);
}

zx_status_t SdioFunctionDevice::SdioUpdateBlockSize(uint8_t fn_idx, uint16_t blk_sz, bool deflt) {
    return sdio_parent_->SdioUpdateBlockSize(fn_idx, blk_sz, deflt);
}

zx_status_t SdioFunctionDevice::SdioGetBlockSize(uint8_t fn_idx, uint16_t* out_cur_blk_size) {
    return sdio_parent_->SdioGetBlockSize(fn_idx, out_cur_blk_size);
}

zx_status_t SdioFunctionDevice::SdioDoRwTxn(uint8_t fn_idx, sdio_rw_txn_t* txn) {
    return sdio_parent_->SdioDoRwTxn(fn_idx, txn);
}

zx_status_t SdioFunctionDevice::SdioDoRwByte(bool write, uint8_t fn_idx, uint32_t addr,
                                             uint8_t write_byte, uint8_t* out_read_byte) {
    return sdio_parent_->SdioDoRwByte(write, fn_idx, addr, write_byte, out_read_byte);
}

zx_status_t SdioFunctionDevice::SdioGetInBandIntr(uint8_t fn_idx, zx::interrupt* out_irq) {
    return sdio_parent_->SdioGetInBandIntr(fn_idx, out_irq);
}

zx_status_t SdioFunctionDevice::SdioIoAbort(uint8_t fn_idx) {
    return sdio_parent_->SdioIoAbort(fn_idx);
}

zx_status_t SdioFunctionDevice::SdioIntrPending(uint8_t fn_idx, bool* out_pending) {
    return sdio_parent_->SdioIntrPending(fn_idx, out_pending);
}

zx_status_t SdioFunctionDevice::SdioDoVendorControlRwByte(bool write, uint8_t addr,
                                                          uint8_t write_byte,
                                                          uint8_t* out_read_byte) {
    return sdio_parent_->SdioDoVendorControlRwByte(write, addr, write_byte, out_read_byte);
}

}  // namespace sdmmc
