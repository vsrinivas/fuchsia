// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/block-verity/verified-device.h"

#include <lib/zx/vmo.h>
#include <zircon/status.h>

#include <ddk/debug.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "src/devices/block/drivers/block-verity/constants.h"
#include "src/devices/block/drivers/block-verity/device-info.h"
#include "src/devices/block/drivers/block-verity/extra.h"

namespace block_verity {
namespace {

static void BlockLoaderCallbackImpl(void* cookie, zx_status_t status, block_op_t* block) {
  VerifiedDevice* device = static_cast<VerifiedDevice*>(cookie);
  device->OnBlockLoaderRequestComplete(status, block);
}

static void ClientBlockCallback(void* cookie, zx_status_t status, block_op_t* block) {
  VerifiedDevice* device = static_cast<VerifiedDevice*>(cookie);
  device->OnClientBlockRequestComplete(status, block);
}

static void BlockVerifierPrepareCallback(void* cookie, zx_status_t status) {
  VerifiedDevice* device = static_cast<VerifiedDevice*>(cookie);
  device->OnBlockVerifierPrepareComplete(status);
}

}  // namespace

// Implementation of the `verified` read-only block device that maps blocks and
// verifies their hashes against integrity data before returning successful
// reads.
VerifiedDevice::VerifiedDevice(zx_device_t* parent, DeviceInfo&& info,
                               const std::array<uint8_t, kHashOutputSize>& integrity_root_hash)
    : VerifiedDeviceType(parent),
      state_(kInitial),
      outstanding_block_requests_(0),
      info_(std::move(info)),
      block_verifier_(info.geometry, integrity_root_hash, this) {
  block_op_buf_ = std::make_unique<uint8_t[]>(info_.op_size);
  list_initialize(&deferred_requests_);
}

zx_status_t VerifiedDevice::Init() {
  {
    // Scope to avoid holding lock when PrepareAsync callback is called
    fbl::AutoLock lock(&mtx_);

    ZX_ASSERT(state_ == kInitial);

    state_ = kLoading;
  }

  zx_status_t rc = block_verifier_.PrepareAsync(this, BlockVerifierPrepareCallback);

  if (rc != ZX_OK) {
    fbl::AutoLock lock(&mtx_);
    state_ = kFailed;
  }

  return rc;
}

zx_status_t VerifiedDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out);
  proto->ctx = this;
  switch (proto_id) {
    case ZX_PROTOCOL_BLOCK_IMPL:
      proto->ops = &block_impl_protocol_ops_;
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_ERR_NOT_SUPPORTED;
}

zx_off_t VerifiedDevice::DdkGetSize() {
  zx_off_t data_size;
  if (mul_overflow(info_.geometry.block_size_, info_.geometry.allocation_.data_block_count,
                   &data_size)) {
    zxlogf(ERROR, "overflowed when computing device size");
    return 0;
  }

  return data_size;
}

void VerifiedDevice::DdkUnbind(ddk::UnbindTxn txn) {
  fbl::AutoLock lock(&mtx_);
  // Change internal state to stop servicing new block requests.
  if (state_ == kFailed) {
    txn.Reply();
    return;
  } else {
    state_ = kQuiescing;
    // Save `txn` so we can reply once outstanding block requests complete.
    unbind_txn_ = std::move(txn);
  }

  TeardownIfQuiesced();
}

void VerifiedDevice::DdkRelease() { delete this; }

void VerifiedDevice::BlockImplQuery(block_info_t* out_info, size_t* out_op_size) {
  info_.block_protocol.Query(out_info, out_op_size);
  // Overwrite block_count with just the number of blocks we're exposing as data
  // blocks.  We keep the superblock & integrity blocks to ourselves.
  // Besides block count and the op size, we're happy to pass through all values
  // from the underlying block device here.
  out_info->block_count = info_.geometry.allocation_.data_block_count;
  out_info->block_size = kBlockSize;
  *out_op_size = info_.op_size;
}

void VerifiedDevice::BlockImplQueue(block_op_t* block_op, block_impl_queue_callback completion_cb,
                                    void* cookie) {
  fbl::AutoLock lock(&mtx_);

  extra_op_t* extra = BlockToExtra(block_op, info_.op_size);
  // Save original values in extra, and adjust block_op's block/vmo offsets.
  uint64_t data_start_offset = info_.geometry.AbsoluteLocationForData(0);
  zx_status_t rc = extra->Init(block_op, completion_cb, cookie, info_.hw_blocks_per_virtual_block,
                               data_start_offset);
  if (rc != ZX_OK) {
    zxlogf(ERROR, "failed to initialize extra info: %s", zx_status_get_string(rc));
    BlockComplete(block_op, rc);
    return;
  }

  // Check state_ and forward if we're active, queue if we're loading, and otherwise reject
  switch (state_) {
    case kInitial:
    case kQuiescing:
    case kStopped:
    case kFailed:
      zxlogf(WARNING, "rejecting block IO due to bad state: %d", state_);
      BlockComplete(block_op, ZX_ERR_BAD_STATE);
      break;
    case kLoading:
      // Defer sending the translated block request until we've finished loading
      // integrity data.
      list_add_tail(&deferred_requests_, &extra->node);
      break;
    case kActive:
      ForwardTranslatedBlockOp(block_op);
      break;
  }
}

void VerifiedDevice::RequestBlocks(uint64_t start_block, uint64_t block_count, zx::vmo& vmo,
                                   void* cookie, BlockLoaderCallback callback) {
  fbl::AutoLock lock(&mtx_);
  block_op_t* block_op = reinterpret_cast<block_op_t*>(block_op_buf_.get());
  block_op->rw.command = BLOCK_OP_READ;
  block_op->rw.length = block_count * info_.hw_blocks_per_virtual_block;
  block_op->rw.offset_dev = start_block * info_.hw_blocks_per_virtual_block;
  block_op->rw.offset_vmo = 0;
  block_op->rw.vmo = vmo.get();

  extra_op_t* extra = BlockToExtra(block_op, info_.op_size);
  extra->cookie = cookie;
  extra->loader_cb = callback;

  outstanding_block_requests_++;
  info_.block_protocol.Queue(block_op, BlockLoaderCallbackImpl, this);
}

void VerifiedDevice::OnBlockLoaderRequestComplete(zx_status_t status, block_op_t* block) {
  {
    // Only need to hold the lock while updating outstanding_block_requests_.
    fbl::AutoLock lock(&mtx_);
    outstanding_block_requests_--;
  }

  extra_op_t* extra = BlockToExtra(block, info_.op_size);
  extra->loader_cb(extra->cookie, status);
}

void VerifiedDevice::ForwardTranslatedBlockOp(block_op_t* block_op) {
  switch (block_op->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
      // Bounds check.  Don't forward reads that would go past the end of the
      // device.  The translated request is in physical blocks.
      if ((block_op->rw.offset_dev + block_op->rw.length) >
          (info_.geometry.total_blocks_ * info_.hw_blocks_per_virtual_block)) {
        BlockComplete(block_op, ZX_ERR_INVALID_ARGS);
        return;
      }

      // Queue to backing block device.
      outstanding_block_requests_++;
      info_.block_protocol.Queue(block_op, ClientBlockCallback, this);
      break;
    case BLOCK_OP_FLUSH:
    case BLOCK_OP_WRITE:
    case BLOCK_OP_TRIM:
      // Writes, TRIM, and flush don't make sense on a read-only device.
      BlockComplete(block_op, ZX_ERR_NOT_SUPPORTED);
      break;
    default:
      // Unknown block command, not sure if this is safe to pass through
      BlockComplete(block_op, ZX_ERR_NOT_SUPPORTED);
  }
}

void VerifiedDevice::OnClientBlockRequestComplete(zx_status_t status, block_op_t* block) {
  fbl::AutoLock lock(&mtx_);
  outstanding_block_requests_--;

  // Restore data that may have changed
  extra_op_t* extra = BlockToExtra(block, info_.op_size);
  block->rw.vmo = extra->vmo;
  block->rw.length = extra->length;
  block->rw.offset_dev = extra->offset_dev;
  block->rw.offset_vmo = extra->offset_vmo;

  if (status != ZX_OK) {
    zxlogf(DEBUG, "parent device returned %s", zx_status_get_string(status));
    BlockComplete(block, status);
    return;
  }

  // Verify each block that we read against the hash from the integrity data.
  for (uint32_t block_offset = 0; block_offset < block->rw.length; block_offset++) {
    uint8_t buf[kBlockSize];
    off_t vmo_offset = extra->offset_vmo + (block_offset * kBlockSize);
    status = zx_vmo_read(extra->vmo, buf, vmo_offset, kBlockSize);
    if (status != ZX_OK) {
      zxlogf(WARNING, "Couldn't read from VMO to verify block data: %s",
             zx_status_get_string(status));
      BlockComplete(block, status);
      return;
    }

    // Check integrity of the block with BlockVerifier.
    // The offset given is the index into the data block section.
    uint64_t data_block_index = extra->offset_dev + block_offset;
    status = block_verifier_.VerifyDataBlockSync(data_block_index, buf);
    if (status != ZX_OK) {
      BlockComplete(block, status);
      return;
    }
  }

  BlockComplete(block, ZX_OK);
}

void VerifiedDevice::OnBlockVerifierPrepareComplete(zx_status_t status) {
  fbl::AutoLock lock(&mtx_);
  ZX_ASSERT(state_ == kLoading);
  state_ = kActive;

  // Cool, the verifier is ready to service requests.  Forward all deferred block
  // ops to the underlying block device.
  while (!list_is_empty(&deferred_requests_)) {
    // Take head of deferred_requests_ as the extra_op_t than holds it
    extra_op_t* extra = list_remove_head_type(&deferred_requests_, extra_op_t, node);
    // turn extra into block_op_t*
    block_op_t* block = ExtraToBlock(extra, info_.op_size);

    // Forward the request
    ForwardTranslatedBlockOp(block);
  }
}

void VerifiedDevice::BlockComplete(block_op_t* block, zx_status_t status) {
  extra_op_t* extra = BlockToExtra(block, info_.op_size);
  // Complete the request.
  extra->completion_cb(extra->cookie, status, block);

  if (state_ == kQuiescing) {
    TeardownIfQuiesced();
  }
}

void VerifiedDevice::TeardownIfQuiesced() {
  if (outstanding_block_requests_ == 0) {
    state_ = kStopped;
    unbind_txn_->Reply();
    unbind_txn_ = std::nullopt;
  }
}

}  // namespace block_verity
