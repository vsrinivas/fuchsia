// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdio-function-device.h"

#include <zircon/driver/binding.h>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>

#include "sdio-controller-device.h"

namespace sdmmc {

zx_status_t SdioFunctionDevice::Create(zx_device_t* parent, SdioControllerDevice* sdio_parent,
                                       std::unique_ptr<SdioFunctionDevice>* out_dev) {
  fbl::AllocChecker ac;
  out_dev->reset(new (&ac) SdioFunctionDevice(parent, sdio_parent));
  if (!ac.check()) {
    zxlogf(ERROR, "sdmmc: failed to allocate device memory\n");
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
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

  function_ = static_cast<uint8_t>(func);

  return st;
}

zx_status_t SdioFunctionDevice::SdioGetDevHwInfo(sdio_hw_info_t* out_hw_info) {
  return sdio_parent_->SdioGetDevHwInfo(out_hw_info);
}

zx_status_t SdioFunctionDevice::SdioEnableFn() { return sdio_parent_->SdioEnableFn(function_); }

zx_status_t SdioFunctionDevice::SdioDisableFn() { return sdio_parent_->SdioDisableFn(function_); }

zx_status_t SdioFunctionDevice::SdioEnableFnIntr() {
  return sdio_parent_->SdioEnableFnIntr(function_);
}

zx_status_t SdioFunctionDevice::SdioDisableFnIntr() {
  return sdio_parent_->SdioDisableFnIntr(function_);
}

zx_status_t SdioFunctionDevice::SdioUpdateBlockSize(uint16_t blk_sz, bool deflt) {
  return sdio_parent_->SdioUpdateBlockSize(function_, blk_sz, deflt);
}

zx_status_t SdioFunctionDevice::SdioGetBlockSize(uint16_t* out_cur_blk_size) {
  return sdio_parent_->SdioGetBlockSize(function_, out_cur_blk_size);
}

zx_status_t SdioFunctionDevice::SdioDoRwTxn(sdio_rw_txn_t* txn) {
  return sdio_parent_->SdioDoRwTxn(function_, txn);
}

zx_status_t SdioFunctionDevice::SdioDoRwByte(bool write, uint32_t addr, uint8_t write_byte,
                                             uint8_t* out_read_byte) {
  return sdio_parent_->SdioDoRwByte(write, function_, addr, write_byte, out_read_byte);
}

zx_status_t SdioFunctionDevice::SdioGetInBandIntr(zx::interrupt* out_irq) {
  return sdio_parent_->SdioGetInBandIntr(function_, out_irq);
}

zx_status_t SdioFunctionDevice::SdioIoAbort() { return sdio_parent_->SdioIoAbort(function_); }

zx_status_t SdioFunctionDevice::SdioIntrPending(bool* out_pending) {
  return sdio_parent_->SdioIntrPending(function_, out_pending);
}

zx_status_t SdioFunctionDevice::SdioDoVendorControlRwByte(bool write, uint8_t addr,
                                                          uint8_t write_byte,
                                                          uint8_t* out_read_byte) {
  return sdio_parent_->SdioDoVendorControlRwByte(write, addr, write_byte, out_read_byte);
}

}  // namespace sdmmc
