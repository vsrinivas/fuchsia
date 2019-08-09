// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/rawnand.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/bti.h>

namespace rawnand {

class CadenceHpnfc;
using DeviceType = ddk::Device<CadenceHpnfc, ddk::Unbindable>;

class CadenceHpnfc : public DeviceType,
                     public ddk::RawNandProtocol<CadenceHpnfc, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  CadenceHpnfc(zx_device_t* parent, ddk::MmioBuffer mmio, ddk::MmioBuffer fifo_mmio, zx::bti bti)
      : DeviceType(parent),
        mmio_(std::move(mmio)),
        fifo_mmio_(std::move(fifo_mmio)),
        bti_(std::move(bti)) {}

  void DdkRelease() { delete this; }
  void DdkUnbind() { DdkRemove(); }

  zx_status_t RawNandReadPageHwecc(uint32_t nandpage, void* out_data_buffer, size_t data_size,
                                   size_t* out_data_actual, void* out_oob_buffer, size_t oob_size,
                                   size_t* out_oob_actual, uint32_t* out_ecc_correct);
  zx_status_t RawNandWritePageHwecc(const void* data_buffer, size_t data_size,
                                    const void* oob_buffer, size_t oob_size, uint32_t nandpage);
  zx_status_t RawNandEraseBlock(uint32_t nandpage);
  zx_status_t RawNandGetNandInfo(nand_info_t* out_info);

 private:
  zx_status_t Init();

  zx_status_t PopulateNandInfoJedec();
  zx_status_t PopulateNandInfoOnfi();

  zx_status_t DoGenericCommand(uint32_t instruction, uint8_t* out_data, uint32_t size);

  // Copy data to or from the FIFO. size is the total number of bytes expected. CopyFromFifo returns
  // the number of bytes read into buffer, which may be zero if buffer is null.
  size_t CopyFromFifo(void* buffer, size_t size);
  void CopyToFifo(const void* buffer, size_t size);

  bool WaitForRBn();
  bool WaitForThread();
  bool WaitForSdmaTrigger();
  bool WaitForCommandComplete();

  ddk::MmioBuffer mmio_;
  ddk::MmioBuffer fifo_mmio_;
  zx::bti bti_;
  nand_info_t nand_info_;
};

}  // namespace rawnand
