// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_SDMMC_SDIO_FUNCTION_DEVICE_H_
#define SRC_STORAGE_BLOCK_DRIVERS_SDMMC_SDIO_FUNCTION_DEVICE_H_

#include <atomic>
#include <memory>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/sdio.h>
#include <fuchsia/hardware/sdio/llcpp/fidl.h>

namespace sdmmc {

using ::llcpp::fuchsia::hardware::sdio::SdioRwTxn;

class SdioControllerDevice;

class SdioFunctionDevice;
using SdioFunctionDeviceType = ddk::Device<SdioFunctionDevice, ddk::Messageable>;

class SdioFunctionDevice : public SdioFunctionDeviceType,
                           public ddk::SdioProtocol<SdioFunctionDevice, ddk::base_protocol>,
                           public ::llcpp::fuchsia::hardware::sdio::Device::Interface {
 public:
  SdioFunctionDevice(zx_device_t* parent, SdioControllerDevice* sdio_parent)
      : SdioFunctionDeviceType(parent), sdio_parent_(sdio_parent) {}

  static zx_status_t Create(zx_device_t* parent, SdioControllerDevice* sdio_parent,
                            std::unique_ptr<SdioFunctionDevice>* out_dev);

  void DdkRelease() { delete this; }

  zx_status_t AddDevice(const sdio_func_hw_info_t& hw_info, uint32_t func);

  zx_status_t SdioGetDevHwInfo(sdio_hw_info_t* out_hw_info);
  zx_status_t SdioEnableFn();
  zx_status_t SdioDisableFn();
  zx_status_t SdioEnableFnIntr();
  zx_status_t SdioDisableFnIntr();
  zx_status_t SdioUpdateBlockSize(uint16_t blk_sz, bool deflt);
  zx_status_t SdioGetBlockSize(uint16_t* out_cur_blk_size);
  zx_status_t SdioDoRwTxn(sdio_rw_txn_t* txn);
  zx_status_t SdioDoRwByte(bool write, uint32_t addr, uint8_t write_byte, uint8_t* out_read_byte);
  zx_status_t SdioGetInBandIntr(zx::interrupt* out_irq);
  zx_status_t SdioIoAbort();
  zx_status_t SdioIntrPending(bool* out_pending);
  zx_status_t SdioDoVendorControlRwByte(bool write, uint8_t addr, uint8_t write_byte,
                                        uint8_t* out_read_byte);
  zx_status_t SdioRegisterVmo(uint32_t vmo_id, zx::vmo vmo, uint64_t offset, uint64_t size);
  zx_status_t SdioUnregisterVmo(uint32_t vmo_id, zx::vmo* out_vmo);
  zx_status_t SdioDoRwTxnNew(const sdio_rw_txn_new_t* txn);

  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    ::llcpp::fuchsia::hardware::sdio::Device::Dispatch(this, msg, &transaction);
    return transaction.Status();
  }

  // FIDL methods
  void GetDevHwInfo(GetDevHwInfoCompleter::Sync completer) override;
  void EnableFn(EnableFnCompleter::Sync completer) override;
  void DisableFn(DisableFnCompleter::Sync completer) override;
  void EnableFnIntr(EnableFnIntrCompleter::Sync completer) override;
  void DisableFnIntr(DisableFnIntrCompleter::Sync completer) override;
  void UpdateBlockSize(uint16_t blk_sz, bool deflt,
                       UpdateBlockSizeCompleter::Sync completer) override;
  void GetBlockSize(GetBlockSizeCompleter::Sync completer) override;
  void DoRwTxn(SdioRwTxn txn, DoRwTxnCompleter::Sync completer) override;
  void DoRwByte(bool write, uint32_t addr, uint8_t write_byte,
                DoRwByteCompleter::Sync completer) override;
  void GetInBandIntr(GetInBandIntrCompleter::Sync completer) override;
  void IoAbort(IoAbortCompleter::Sync completer) override;
  void IntrPending(IntrPendingCompleter::Sync completer) override;
  void DoVendorControlRwByte(bool write, uint8_t addr, uint8_t write_byte,
                             DoVendorControlRwByteCompleter::Sync completer) override;

 private:
  uint8_t function_ = SDIO_MAX_FUNCS;
  SdioControllerDevice* sdio_parent_;
};

}  // namespace sdmmc

#endif  // SRC_STORAGE_BLOCK_DRIVERS_SDMMC_SDIO_FUNCTION_DEVICE_H_
