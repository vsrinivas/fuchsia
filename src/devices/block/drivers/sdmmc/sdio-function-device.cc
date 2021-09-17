// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdio-function-device.h"

#include <lib/ddk/debug.h>

#include <fbl/alloc_checker.h>

#include "sdio-controller-device.h"
#include "src/devices/block/drivers/sdmmc/sdmmc-bind.h"

namespace sdmmc {

using fuchsia_hardware_sdio::wire::SdioHwInfo;

zx_status_t SdioFunctionDevice::Create(zx_device_t* parent, SdioControllerDevice* sdio_parent,
                                       std::unique_ptr<SdioFunctionDevice>* out_dev) {
  fbl::AllocChecker ac;
  out_dev->reset(new (&ac) SdioFunctionDevice(parent, sdio_parent));
  if (!ac.check()) {
    zxlogf(ERROR, "failed to allocate device memory");
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
  zx_status_t st = DdkAdd(ddk::DeviceAddArgs(name).set_props(props));
  if (st != ZX_OK) {
    zxlogf(ERROR, "Failed to add sdio device, retcode = %d", st);
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

void SdioFunctionDevice::GetDevHwInfo(GetDevHwInfoRequestView request,
                                      GetDevHwInfoCompleter::Sync& completer) {
  sdio_hw_info_t hw_info = {};
  zx_status_t status = SdioGetDevHwInfo(&hw_info);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }

  SdioHwInfo fidl_hw_info;
  static_assert(sizeof(fidl_hw_info) == sizeof(hw_info));
  memcpy(&fidl_hw_info, &hw_info, sizeof(fidl_hw_info));

  completer.ReplySuccess(fidl_hw_info);
}

void SdioFunctionDevice::EnableFn(EnableFnRequestView request, EnableFnCompleter::Sync& completer) {
  zx_status_t status = SdioEnableFn();
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void SdioFunctionDevice::DisableFn(DisableFnRequestView request,
                                   DisableFnCompleter::Sync& completer) {
  zx_status_t status = SdioDisableFn();
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void SdioFunctionDevice::EnableFnIntr(EnableFnIntrRequestView request,
                                      EnableFnIntrCompleter::Sync& completer) {
  zx_status_t status = SdioEnableFnIntr();
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void SdioFunctionDevice::DisableFnIntr(DisableFnIntrRequestView request,
                                       DisableFnIntrCompleter::Sync& completer) {
  zx_status_t status = SdioDisableFnIntr();
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void SdioFunctionDevice::UpdateBlockSize(UpdateBlockSizeRequestView request,
                                         UpdateBlockSizeCompleter::Sync& completer) {
  zx_status_t status = SdioUpdateBlockSize(request->blk_sz, request->deflt);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void SdioFunctionDevice::GetBlockSize(GetBlockSizeRequestView request,
                                      GetBlockSizeCompleter::Sync& completer) {
  uint16_t cur_blk_size;
  zx_status_t status = SdioGetBlockSize(&cur_blk_size);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess(cur_blk_size);
}

void SdioFunctionDevice::DoRwTxn(DoRwTxnRequestView request, DoRwTxnCompleter::Sync& completer) {
  sdio_rw_txn_t sdio_txn = {};
  sdio_txn.addr = request->txn.addr;
  sdio_txn.data_size = request->txn.data_size;
  sdio_txn.incr = request->txn.incr;
  sdio_txn.write = request->txn.write;
  sdio_txn.use_dma = request->txn.use_dma;
  sdio_txn.buf_offset = request->txn.buf_offset;
  if (request->txn.use_dma) {
    sdio_txn.dma_vmo = request->txn.dma_vmo.get();
    sdio_txn.virt_buffer = nullptr;
    sdio_txn.virt_size = 0;
  } else {
    sdio_txn.dma_vmo = ZX_HANDLE_INVALID;
    sdio_txn.virt_buffer = request->txn.virt.mutable_data();
    sdio_txn.virt_size = request->txn.virt.count();
  }

  zx_status_t status = SdioDoRwTxn(&sdio_txn);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess(std::move(request->txn));
}

void SdioFunctionDevice::DoRwByte(DoRwByteRequestView request, DoRwByteCompleter::Sync& completer) {
  zx_status_t status =
      SdioDoRwByte(request->write, request->addr, request->write_byte, &request->write_byte);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess(request->write_byte);
}

void SdioFunctionDevice::GetInBandIntr(GetInBandIntrRequestView request,
                                       GetInBandIntrCompleter::Sync& completer) {
  zx::interrupt irq;
  zx_status_t status = SdioGetInBandIntr(&irq);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess(std::move(irq));
}

void SdioFunctionDevice::IoAbort(IoAbortRequestView request, IoAbortCompleter::Sync& completer) {
  zx_status_t status = SdioIoAbort();
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void SdioFunctionDevice::IntrPending(IntrPendingRequestView request,
                                     IntrPendingCompleter::Sync& completer) {
  bool pending;
  zx_status_t status = SdioIntrPending(&pending);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess(pending);
}

void SdioFunctionDevice::DoVendorControlRwByte(DoVendorControlRwByteRequestView request,
                                               DoVendorControlRwByteCompleter::Sync& completer) {
  zx_status_t status = SdioDoVendorControlRwByte(request->write, request->addr, request->write_byte,
                                                 &request->write_byte);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess(request->write_byte);
}

zx_status_t SdioFunctionDevice::SdioRegisterVmo(uint32_t vmo_id, zx::vmo vmo, uint64_t offset,
                                                uint64_t size, uint32_t vmo_rights) {
  return sdio_parent_->SdioRegisterVmo(function_, vmo_id, std::move(vmo), offset, size, vmo_rights);
}

zx_status_t SdioFunctionDevice::SdioUnregisterVmo(uint32_t vmo_id, zx::vmo* out_vmo) {
  return sdio_parent_->SdioUnregisterVmo(function_, vmo_id, out_vmo);
}

zx_status_t SdioFunctionDevice::SdioDoRwTxnNew(const sdio_rw_txn_new_t* txn) {
  return sdio_parent_->SdioDoRwTxnNew(function_, txn);
}

void SdioFunctionDevice::SdioRunDiagnostics() { return sdio_parent_->SdioRunDiagnostics(); }

}  // namespace sdmmc
