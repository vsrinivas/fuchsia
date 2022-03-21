// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_NAND_DRIVERS_INTEL_SPI_FLASH_INTEL_SPI_FLASH_H_
#define SRC_DEVICES_NAND_DRIVERS_INTEL_SPI_FLASH_INTEL_SPI_FLASH_H_

#include <fuchsia/hardware/nand/cpp/banjo.h>
#include <lib/device-protocol/pci.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sync/completion.h>
#include <lib/zx/status.h>

#include <condition_variable>
#include <thread>

#include <ddktl/device.h>

#include "src/devices/lib/mmio/include/lib/mmio/mmio.h"
#include "src/devices/nand/drivers/intel-spi-flash/flash-chips.h"
#include "src/devices/nand/drivers/intel-spi-flash/registers.h"

namespace spiflash {

class SpiFlashDevice;
using DeviceType = ddk::Device<SpiFlashDevice, ddk::Unbindable>;

class SpiFlashDevice : public DeviceType,
                       public ddk::NandProtocol<SpiFlashDevice, ddk::base_protocol> {
 public:
  SpiFlashDevice(zx_device_t* parent, fdf::MmioBuffer mmio)
      : DeviceType(parent), mmio_(std::move(mmio)) {}

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease() { delete this; }

  // Nand protocol implementation.
  void NandQuery(nand_info_t* info_out, size_t* nand_op_size_out);
  void NandQueue(nand_operation_t* op, nand_queue_callback completion_cb, void* cookie);
  zx_status_t NandGetFactoryBadBlockList(uint32_t* bad_blocks, size_t bad_block_len,
                                         size_t* num_bad_blocks) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Public for testing.
  zx_status_t Bind();
  void StartShutdown() __TA_EXCLUDES(io_queue_mutex_);

 private:
  // Represents an operation queued via NandQueue().
  struct IoOp {
    nand_operation_t* op;
    nand_queue_callback completion_cb;
    void* cookie;
  };

  std::optional<FlashChipInfo> DetermineFlashChip();
  // Returns true on success, false if the command failed.
  bool PollCommandComplete();

  void IoThread();
  void HandleOp(IoOp& op);
  zx_status_t NandRead(uint32_t address, size_t length, size_t vmo_offset, zx::unowned_vmo dst_vmo);
  zx_status_t NandErase(uint32_t block, size_t num_blocks);
  zx_status_t NandWriteBytes(uint64_t address, size_t length, size_t vmo_offset,
                             zx::unowned_vmo dst_vmo);

  fdf::MmioBuffer mmio_;
  inspect::Inspector inspect_;

  std::optional<FlashChipInfo> flash_chip_;

  // mmio_mutex and io_queue_mutex should not be held at the same time.
  std::mutex io_queue_mutex_;
  std::condition_variable_any condition_;
  bool shutdown_ __TA_GUARDED(io_queue_mutex_) = false;
  std::vector<IoOp> io_queue_ __TA_GUARDED(io_queue_mutex_);
  std::thread io_thread_;
};

}  // namespace spiflash

#endif  // SRC_DEVICES_NAND_DRIVERS_INTEL_SPI_FLASH_INTEL_SPI_FLASH_H_
