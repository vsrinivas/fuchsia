// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-spi-flash.h"

#include <fuchsia/hardware/nandinfo/c/banjo.h>
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <zircon/errors.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <cstddef>

#include <ddktl/device.h>
#include <safemath/checked_math.h>

#include "src/devices/nand/drivers/intel-spi-flash/flash-chips.h"
#include "src/devices/nand/drivers/intel-spi-flash/intel_spi_flash_bind.h"
#include "src/devices/nand/drivers/intel-spi-flash/registers.h"

// This driver is written against the "7th and 8th Generation Intel® Processor Family I/O for U/Y
// Platforms and 10th Generation Intel® Processor Family I/O for Y Platforms" datasheet, volume 2,
// section 8 "SPI Interface".
// Intel document number 334659.

namespace spiflash {
constexpr size_t kKilobyte = 1024;
constexpr uint32_t kEraseBlockSize = 4 * kKilobyte;
constexpr size_t kMaxBurstSize = 64;

zx_status_t SpiFlashDevice::Bind() {
  // Make sure that the flash device is valid.
  if (!FlashControl::Get().ReadFrom(&mmio_).fdv()) {
    zxlogf(ERROR, "Invalid flash descriptor.");
    return ZX_ERR_NOT_SUPPORTED;
  }
  // And make sure that we recognise it.
  flash_chip_ = DetermineFlashChip();
  if (!flash_chip_.has_value()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  // The MMIO interface wants 32-bit reads/writes, so make sure that all I/O operations (which are
  // specified in terms of pages) are going to align nicely to 32 bits.
  ZX_ASSERT(flash_chip_->page_size % sizeof(uint32_t) == 0);
  zxlogf(INFO, "Found flash chip '%.*s'.", static_cast<int>(flash_chip_->name.size()),
         flash_chip_->name.data());

  io_thread_ = std::thread(&SpiFlashDevice::IoThread, this);

  zx_device_prop_t props[] = {
      {
          .id = BIND_NAND_CLASS,
          .value = NAND_CLASS_INTEL_FLASH_DESCRIPTOR,
      },
  };
  return DdkAdd(ddk::DeviceAddArgs("intel-spi-flash")
                    .set_inspect_vmo(inspect_.DuplicateVmo())
                    .set_props(props));
}

void SpiFlashDevice::StartShutdown() {
  std::scoped_lock lock(io_queue_mutex_);
  shutdown_ = true;
  condition_.notify_all();
}

void SpiFlashDevice::DdkUnbind(ddk::UnbindTxn txn) {
  StartShutdown();
  if (io_thread_.joinable()) {
    io_thread_.join();
  }
  std::vector<IoOp> queue;
  {
    std::scoped_lock lock(io_queue_mutex_);
    std::swap(io_queue_, queue);
  }
  for (auto &item : queue) {
    item.completion_cb(item.cookie, ZX_ERR_UNAVAILABLE, item.op);
  }
  txn.Reply();
}

void SpiFlashDevice::NandQuery(nand_info_t *info_out, size_t *nand_op_size_out) {
  *nand_op_size_out = sizeof(nand_operation_t);
  *info_out = {
      .page_size = flash_chip_->page_size,
      // pages_per_block is used to determine erase size. The controller always supports a 4k erase
      // granularity, so we just figure out how many pages fit in 4KiB.
      .pages_per_block = static_cast<uint32_t>(kEraseBlockSize / flash_chip_->page_size),
      .num_blocks = static_cast<uint32_t>(flash_chip_->size / kEraseBlockSize),
      .nand_class = NAND_CLASS_INTEL_FLASH_DESCRIPTOR,
  };
}

void SpiFlashDevice::NandQueue(nand_operation_t *op, nand_queue_callback completion_cb,
                               void *cookie) {
  bool shutdown = false;
  {
    std::scoped_lock lock(io_queue_mutex_);
    if (!shutdown_) {
      io_queue_.emplace_back(IoOp{
          .op = op,
          .completion_cb = completion_cb,
          .cookie = cookie,
      });
    } else {
      shutdown = true;
    }
  }
  if (shutdown) {
    completion_cb(cookie, ZX_ERR_UNAVAILABLE, op);
  } else {
    condition_.notify_all();
  }
}

void SpiFlashDevice::IoThread() {
  while (true) {
    std::vector<IoOp> queue;
    {
      std::scoped_lock lock(io_queue_mutex_);
      condition_.wait(io_queue_mutex_, [&]() __TA_REQUIRES(io_queue_mutex_) {
        return !io_queue_.empty() || shutdown_;
      });

      if (shutdown_) {
        break;
      }

      std::swap(io_queue_, queue);
    }

    for (auto &op : queue) {
      HandleOp(op);
    }
  }
}

void SpiFlashDevice::HandleOp(IoOp &op) {
  switch (op.op->command) {
    case NAND_OP_WRITE_BYTES: {
      zx_status_t status = NandWriteBytes(op.op->rw_bytes.offset_nand, op.op->rw_bytes.length,
                                          op.op->rw_bytes.offset_data_vmo,
                                          zx::unowned_vmo(op.op->rw_bytes.data_vmo));
      op.completion_cb(op.cookie, status, op.op);
      break;
    }
    case NAND_OP_ERASE: {
      zx_status_t status = NandErase(op.op->erase.first_block, op.op->erase.num_blocks);
      op.completion_cb(op.cookie, status, op.op);
      break;
    }
    case NAND_OP_WRITE: {
      zx_status_t status = NandWriteBytes(
          static_cast<uint64_t>(op.op->rw.offset_nand) * flash_chip_->page_size,
          static_cast<uint64_t>(op.op->rw.length) * flash_chip_->page_size,
          op.op->rw.offset_data_vmo * flash_chip_->page_size, zx::unowned_vmo(op.op->rw.data_vmo));
      op.completion_cb(op.cookie, status, op.op);
      break;
    }
    case NAND_OP_READ: {
      op.op->rw.corrected_bit_flips = 0;
      zx_status_t status = NandRead(op.op->rw.offset_nand, op.op->rw.length,
                                    op.op->rw.offset_data_vmo, zx::unowned_vmo(op.op->rw.data_vmo));
      op.completion_cb(op.cookie, status, op.op);
      break;
    }
    default: {
      op.completion_cb(op.cookie, ZX_ERR_NOT_SUPPORTED, op.op);
      break;
    }
  }
}

zx_status_t SpiFlashDevice::NandErase(uint32_t block, size_t num_blocks) {
  uint32_t flash_num_blocks = flash_chip_->size / kEraseBlockSize;
  uint32_t max_block;
  if (!safemath::CheckAdd(block, num_blocks).AssignIfValid(&max_block)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (max_block > flash_num_blocks) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Calculate the start address of the block.
  uint32_t first_block_addr;
  if (!safemath::CheckMul(block, kEraseBlockSize).AssignIfValid(&first_block_addr)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  for (size_t i = 0; i < num_blocks; i++) {
    FlashAddress::Get().FromValue(first_block_addr + (i * kEraseBlockSize)).WriteTo(&mmio_);
    auto reg = FlashControl::Get().ReadFrom(&mmio_);
    // Unset FDONE, FCERR, H_AEL from the last run.
    // Otherwise PollCommandComplete() would immediately return.
    reg.WriteTo(&mmio_).ReadFrom(&mmio_);

    reg.set_fdbc(0).set_fcycle(FlashControl::kErase4k).set_fgo(1);
    reg.WriteTo(&mmio_);

    // The controller hardware handles setting WEL and polling WIP for us.
    // We just have to wait until it's done.
    if (!PollCommandComplete()) {
      return ZX_ERR_IO;
    }
  }

  return ZX_OK;
}

zx_status_t SpiFlashDevice::NandWriteBytes(uint64_t address64, size_t length, size_t vmo_offset,
                                           zx::unowned_vmo src_vmo) {
  uint64_t max;
  if (!safemath::CheckAdd(address64, length).AssignIfValid(&max)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (max > flash_chip_->size) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint8_t bounce_buffer[kMaxBurstSize] = {0};
  // The controller only has a 32-bit register for the address.
  // As a consequence, flash_chip_->size is guaranteed to be <= UINT32_MAX.
  uint32_t address = static_cast<uint32_t>(address64);

  size_t burst;
  for (size_t written = 0; written < length; written += burst) {
    burst = std::min(kMaxBurstSize, length - written);
    zx_status_t status = src_vmo->read(bounce_buffer, vmo_offset + written, burst);
    if (status != ZX_OK) {
      return status;
    }
    uint32_t data = 0;
    size_t i = 0;
    for (i = 0; i < burst; i++) {
      if (i % sizeof(uint32_t) == 0) {
        data = 0;
      }
      // The lowest byte to be written goes at bits 7:0 in the register,
      // the next at bits 15:8, then 23:16, then 31:24. For more information
      // see section 8.2.5 "Flash Data 0" in the datasheet.
      data |= (bounce_buffer[i]) << ((i % sizeof(uint32_t)) * 8);
      if (i % sizeof(uint32_t) == 3) {
        FlashData::Get(i / sizeof(uint32_t)).FromValue(data).WriteTo(&mmio_);
      }
    }

    // Write whatever is left.
    if (i % sizeof(uint32_t) != 0) {
      FlashData::Get(i / sizeof(uint32_t)).FromValue(data).WriteTo(&mmio_);
    }

    FlashAddress::Get().FromValue(address + written).WriteTo(&mmio_);
    auto reg = FlashControl::Get().ReadFrom(&mmio_);
    // Unset FDONE, FCERR, H_AEL from the last run.
    // Otherwise PollCommandComplete() would immediately return.
    reg.WriteTo(&mmio_).ReadFrom(&mmio_);

    reg.set_fdbc(burst - 1).set_fcycle(FlashControl::kWrite).set_fgo(1);
    reg.WriteTo(&mmio_);

    if (!PollCommandComplete()) {
      return ZX_ERR_IO;
    }
  }

  return ZX_OK;
}

zx_status_t SpiFlashDevice::NandRead(uint32_t address, size_t length, size_t vmo_offset,
                                     zx::unowned_vmo dst_vmo) {
  // length and address are both in pages.
  if (!safemath::CheckMul(length, flash_chip_->page_size).AssignIfValid(&length)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (!safemath::CheckMul(address, flash_chip_->page_size).AssignIfValid(&address)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (!safemath::CheckMul(vmo_offset, flash_chip_->page_size).AssignIfValid(&vmo_offset)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (address > flash_chip_->size || length > (flash_chip_->size - address)) {
    zxlogf(ERROR, "Read of 0x%zx at 0x%x goes beyond chip size of 0x%lx", length, address,
           flash_chip_->size);
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint32_t bounce_buffer[kMaxBurstSize / sizeof(uint32_t)];

  size_t read = 0;
  while (read < length) {
    size_t left = length - read;
    size_t burst = std::min(left, kMaxBurstSize);
    FlashAddress::Get().FromValue(address).WriteTo(&mmio_);
    auto reg = FlashControl::Get().ReadFrom(&mmio_);
    // Unset FDONE, FCERR, H_AEL from the last run.
    // Otherwise PollCommandComplete() would immediately return.
    reg.WriteTo(&mmio_).ReadFrom(&mmio_);

    reg.set_fdbc(burst - 1).set_fcycle(FlashControl::kRead).set_fgo(1);
    reg.WriteTo(&mmio_);

    bool ok = PollCommandComplete();
    if (!ok) {
      zxlogf(ERROR, "Failed while reading address 0x%zx from flash chip", read);
      return ZX_ERR_IO;
    }

    // The documentation doesn't specify if register accesses wider than 32 bits are safe, so we do
    // a word-by-word copy.
    for (size_t i = 0; i < (burst / sizeof(uint32_t)); i++) {
      bounce_buffer[i] = FlashData::Get(i).ReadFrom(&mmio_).data();
    }
    // Copy the next chunk into the VMO.
    dst_vmo->write(bounce_buffer, vmo_offset, burst);

    address += burst;
    read += burst;
    vmo_offset += burst;
  }
  return ZX_OK;
}

bool SpiFlashDevice::PollCommandComplete() {
  auto reg = FlashControl::Get().ReadFrom(&mmio_);
  while (!reg.fdone() && !reg.fcerr()) {
    zx::nanosleep(zx::deadline_after(zx::usec(10)));
    reg.ReadFrom(&mmio_);
  }

  return !reg.fcerr();
}

std::optional<FlashChipInfo> SpiFlashDevice::DetermineFlashChip() {
  uint32_t jedec_id;
  {
    // reset address.
    FlashAddress::Get().FromValue(0).WriteTo(&mmio_);
    auto reg = FlashControl::Get().ReadFrom(&mmio_);
    // Clear any stray FDONE etc bits.
    reg.WriteTo(&mmio_).ReadFrom(&mmio_);
    reg.set_fcycle(FlashControl::kReadJedecId);
    // Read four bytes.
    reg.set_fdbc(4);
    reg.set_fgo(1);
    reg.WriteTo(&mmio_);

    bool ok = PollCommandComplete();
    if (!ok) {
      zxlogf(ERROR, "error while reading jedec id");
      return std::nullopt;
    }

    jedec_id = FlashData::Get(0).ReadFrom(&mmio_).data();
  }
  uint16_t vendor_id = jedec_id >> 24;
  uint16_t device_id = jedec_id & 0xff00;
  device_id |= (jedec_id >> 16) & 0xff;
  zxlogf(INFO, "Found SPI flash with vendor: 0x%x device: 0x%x", vendor_id, device_id);

  for (const auto &device : kFlashDevices) {
    if (device.vendor_id == vendor_id && device.device_id == device_id) {
      return device;
    }
  }

  // We could try and determine if the chip has SFDP support,
  // and use that to get the information we need.
  return std::nullopt;
}

static zx_status_t CreateSpiFlash(void *ctx, zx_device_t *parent) {
  ddk::Pci pci(parent, "pci");
  std::optional<ddk::MmioBuffer> mmio;
  zx_status_t status = pci.MapMmio(0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "spiflash failed to map mmio: %d\n", status);
    return status;
  }

  auto ptr = std::make_unique<SpiFlashDevice>(parent, std::move(mmio.value()));
  status = ptr->Bind();
  if (status == ZX_OK) {
    __UNUSED auto unused = ptr.release();
  }
  return status;
}

static zx_driver_ops_t driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = CreateSpiFlash,
};
}  // namespace spiflash

// clang-format off
ZIRCON_DRIVER(intel-spi-flash, spiflash::driver_ops, "zircon", "0.1");
