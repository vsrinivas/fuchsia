// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>

#include <ddktl/device.h>
#include <ddktl/protocol/sdio.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

namespace sdmmc {

class SdioControllerDevice;

class SdioFunctionDevice;
using SdioFunctionDeviceType = ddk::Device<SdioFunctionDevice, ddk::Unbindable>;

class SdioFunctionDevice : public SdioFunctionDeviceType,
                           public ddk::SdioProtocol<SdioFunctionDevice, ddk::base_protocol>,
                           public fbl::RefCounted<SdioFunctionDevice> {
public:
    SdioFunctionDevice(zx_device_t* parent, SdioControllerDevice* sdio_parent)
        : SdioFunctionDeviceType(parent), sdio_parent_(sdio_parent) {}

    static zx_status_t Create(zx_device_t* parent, SdioControllerDevice* sdio_parent,
                              fbl::RefPtr<SdioFunctionDevice>* out_dev);

    void DdkUnbind();
    void DdkRelease();

    zx_status_t AddDevice(const sdio_func_hw_info_t& hw_info, uint32_t func);

    zx_status_t SdioGetDevHwInfo(sdio_hw_info_t* out_hw_info);
    zx_status_t SdioEnableFn(uint8_t fn_idx);
    zx_status_t SdioDisableFn(uint8_t fn_idx);
    zx_status_t SdioEnableFnIntr(uint8_t fn_idx);
    zx_status_t SdioDisableFnIntr(uint8_t fn_idx);
    zx_status_t SdioUpdateBlockSize(uint8_t fn_idx, uint16_t blk_sz, bool deflt);
    zx_status_t SdioGetBlockSize(uint8_t fn_idx, uint16_t* out_cur_blk_size);
    zx_status_t SdioDoRwTxn(uint8_t fn_idx, sdio_rw_txn_t* txn);
    zx_status_t SdioDoRwByte(bool write, uint8_t fn_idx, uint32_t addr, uint8_t write_byte,
                             uint8_t* out_read_byte);
    zx_status_t SdioGetInBandIntr(uint8_t fn_idx, zx::interrupt* out_irq);
    zx_status_t SdioIoAbort(uint8_t fn_idx);
    zx_status_t SdioIntrPending(uint8_t fn_idx, bool* out_pending);
    zx_status_t SdioDoVendorControlRwByte(bool write, uint8_t addr, uint8_t write_byte,
                                          uint8_t* out_read_byte);

private:
    std::atomic<bool> dead_;
    SdioControllerDevice* sdio_parent_;
};

}  // namespace sdmmc
