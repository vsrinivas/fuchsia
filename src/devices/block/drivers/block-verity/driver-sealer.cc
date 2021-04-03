// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/block-verity/driver-sealer.h"

#include <lib/ddk/debug.h>
#include <lib/fit/defer.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/status.h>

namespace block_verity {

// Size of the VMO buffer to allocate.  Chosen so we can read a whole integrity
// block worth of data blocks at a time.  (In practice this is 512KiB.)
constexpr size_t kVmoSize = kBlockSize * (kBlockSize / kHashOutputSize);

DriverSealer::DriverSealer(DeviceInfo info)
    : Sealer(info.geometry), info_(std::move(info)), outstanding_block_requests_(0) {
  block_op_buf_ = std::make_unique<uint8_t[]>(info_.upstream_op_size);
}

DriverSealer::~DriverSealer() {
  // We should not be destructed until all our outstanding block requests have
  // completed, lest they trigger callbacks that refer to the destructed object.
  ZX_ASSERT(outstanding_block_requests_ == 0);

  // Unmap the vmo frmo the vmar.
  if (vmo_base_ == nullptr) {
    return;
  }
  uintptr_t address = reinterpret_cast<uintptr_t>(vmo_base_);
  vmo_base_ = nullptr;
  zx_status_t rc = zx::vmar::root_self()->unmap(address, kVmoSize);
  if (rc != ZX_OK) {
    zxlogf(WARNING, "failed to unmap %lu bytes at %lu: %s", kVmoSize, address,
           zx_status_get_string(rc));
  }
}

zx_status_t DriverSealer::StartSealing(void* cookie, sealer_callback callback) {
  // Allocate and map a VMO for block operations
  zx_status_t rc;
  if ((rc = zx::vmo::create(kVmoSize, 0, &block_op_vmo_)) != ZX_OK) {
    zxlogf(ERROR, "zx::vmo::create failed: %s", zx_status_get_string(rc));
    return rc;
  }
  auto cleanup = fit::defer([this]() { block_op_vmo_.reset(); });
  constexpr uint32_t flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  uintptr_t address;
  if ((rc = zx::vmar::root_self()->map(flags, 0, block_op_vmo_, 0, kVmoSize, &address)) != ZX_OK) {
    zxlogf(ERROR, "zx::vmar::map failed: %s", zx_status_get_string(rc));
    return rc;
  }
  vmo_base_ = reinterpret_cast<uint8_t*>(address);
  cleanup.cancel();

  // Propagate to base class
  return Sealer::StartSealing(cookie, callback);
}

void DriverSealer::RequestRead(uint64_t block) {
  // For now, we'll just read one logical block, though we could move to larger
  // batches ~trivially with a larger block_op_vmo_ buffer.
  block_op_t* block_op = reinterpret_cast<block_op_t*>(block_op_buf_.get());
  block_op->rw.command = BLOCK_OP_READ;
  block_op->rw.length = info_.hw_blocks_per_virtual_block;
  block_op->rw.offset_dev = block * info_.hw_blocks_per_virtual_block;
  block_op->rw.offset_vmo = 0;
  block_op->rw.vmo = block_op_vmo_.get();

  // Send read request.
  outstanding_block_requests_ += 1;
  ZX_ASSERT(outstanding_block_requests_ == 1);
  info_.block_protocol.Queue(block_op, ReadCompletedCallback, this);
}

void DriverSealer::OnReadCompleted(zx_status_t status, block_op_t* block) {
  // Decrement outstanding requests.
  outstanding_block_requests_ -= 1;
  ZX_ASSERT(outstanding_block_requests_ == 0);

  // Check for failures.
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read %d blocks starting at offset %lu: %s", block->rw.length,
           block->rw.offset_dev, zx_status_get_string(status));
    CompleteRead(status, nullptr);
    return;
  }

  CompleteRead(status, vmo_base_);
}

void DriverSealer::ReadCompletedCallback(void* cookie, zx_status_t status, block_op_t* block) {
  // Static trampoline to OnReadCompleted.
  DriverSealer* driver_sealer = static_cast<DriverSealer*>(cookie);
  driver_sealer->OnReadCompleted(status, block);
}

void DriverSealer::WriteIntegrityBlock(HashBlockAccumulator& hba, uint64_t block) {
  // Copy integrity block contents into VMO for write
  memcpy(vmo_base_, hba.BlockData(), kBlockSize);

  // prepare write block op
  block_op_t* block_op = reinterpret_cast<block_op_t*>(block_op_buf_.get());
  block_op->rw.command = BLOCK_OP_WRITE;
  block_op->rw.length = info_.hw_blocks_per_virtual_block;
  block_op->rw.offset_dev = block * info_.hw_blocks_per_virtual_block;
  block_op->rw.offset_vmo = 0;
  block_op->rw.vmo = block_op_vmo_.get();

  // send write request
  outstanding_block_requests_ += 1;
  ZX_ASSERT(outstanding_block_requests_ == 1);
  info_.block_protocol.Queue(block_op, IntegrityWriteCompletedCallback, this);
}

void DriverSealer::OnIntegrityWriteCompleted(zx_status_t status, block_op_t* block) {
  // Decrement outstanding requests.
  outstanding_block_requests_ -= 1;
  ZX_ASSERT(outstanding_block_requests_ == 0);

  // Check for failures.
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write integrity block at device offset %lu: %s", block->rw.offset_dev,
           zx_status_get_string(status));
  }

  CompleteIntegrityWrite(status);
}

void DriverSealer::IntegrityWriteCompletedCallback(void* cookie, zx_status_t status,
                                                   block_op_t* block) {
  // Static trampoline to OnIntegrityWriteCompleted.
  DriverSealer* driver_sealer = static_cast<DriverSealer*>(cookie);
  driver_sealer->OnIntegrityWriteCompleted(status, block);
}

void DriverSealer::WriteSuperblock() {
  PrepareSuperblock(vmo_base_);

  // prepare write block op
  block_op_t* block_op = reinterpret_cast<block_op_t*>(block_op_buf_.get());
  block_op->rw.command = BLOCK_OP_WRITE;
  block_op->rw.length = info_.hw_blocks_per_virtual_block;
  block_op->rw.offset_dev = 0;  // Superblock is block 0
  block_op->rw.offset_vmo = 0;
  block_op->rw.vmo = block_op_vmo_.get();

  // send write request
  outstanding_block_requests_ += 1;
  ZX_ASSERT(outstanding_block_requests_ == 1);
  info_.block_protocol.Queue(block_op, SuperblockWriteCompletedCallback, this);
  return;
}

void DriverSealer::OnSuperblockWriteCompleted(zx_status_t status, block_op_t* block) {
  // Decrement outstanding requests.
  outstanding_block_requests_ -= 1;
  ZX_ASSERT(outstanding_block_requests_ == 0);

  // Check for failures.
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write superblock: %s", zx_status_get_string(status));
  }

  CompleteSuperblockWrite(status);
}

void DriverSealer::SuperblockWriteCompletedCallback(void* cookie, zx_status_t status,
                                                    block_op_t* block) {
  // Static trampoline to OnSuperblockWriteCompleted.
  DriverSealer* driver_sealer = static_cast<DriverSealer*>(cookie);
  driver_sealer->OnSuperblockWriteCompleted(status, block);
}

void DriverSealer::RequestFlush() {
  // prepare flush block op
  block_op_t* block_op = reinterpret_cast<block_op_t*>(block_op_buf_.get());
  block_op->command = BLOCK_OP_FLUSH;

  // send write request
  outstanding_block_requests_ += 1;
  ZX_ASSERT(outstanding_block_requests_ == 1);
  info_.block_protocol.Queue(block_op, FlushCompletedCallback, this);
  return;
}

void DriverSealer::OnFlushCompleted(zx_status_t status, block_op_t* block) {
  // Decrement outstanding requests.
  outstanding_block_requests_ -= 1;
  ZX_ASSERT(outstanding_block_requests_ == 0);

  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to flush: %s", zx_status_get_string(status));
  }

  CompleteFlush(status);
}

void DriverSealer::FlushCompletedCallback(void* cookie, zx_status_t status, block_op_t* block) {
  // Static trampoline to OnFlushCompleted.
  DriverSealer* driver_sealer = static_cast<DriverSealer*>(cookie);
  driver_sealer->OnFlushCompleted(status, block);
}

}  // namespace block_verity
