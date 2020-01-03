// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/buffer/blocking_ring_buffer.h"

#include <lib/fzl/owned-vmo-mapper.h>

#include <fbl/auto_lock.h>

namespace storage {
namespace internal {

BlockingRingBufferImpl::BlockingRingBufferImpl(std::unique_ptr<RingBuffer> buffer)
    : buffer_(std::move(buffer)) {}

zx_status_t BlockingRingBufferImpl::Reserve(uint64_t blocks, BlockingRingBufferReservation* out) {
  // First, ensure that it is possible for us to eventually get space.
  if (blocks > buffer_->capacity()) {
    return ZX_ERR_NO_SPACE;
  }

  RingBufferReservation reservation;

  {
    zx_status_t status = ZX_ERR_NO_SPACE;
    fbl::AutoLock lock(&lock_);
    do {
      status = buffer_->Reserve(blocks, &reservation);
      if (status == ZX_ERR_NO_SPACE) {
        // Case 1: We cannot reserve space. Block until it is available.
        cvar_.Wait(&lock_);
      } else if (status != ZX_OK) {
        // Case 2: We cannot reserve space, but don't expect it to become available.
        return status;
      }
      // Case 3: We reserved space successfully. Fallthrough.
    } while (status == ZX_ERR_NO_SPACE);
  }

  *out = BlockingRingBufferReservation(this, std::move(reservation));
  return ZX_OK;
}

void BlockingRingBufferImpl::Wake() {
  fbl::AutoLock lock(&lock_);
  cvar_.Broadcast();
}

}  // namespace internal

BlockingRingBuffer::BlockingRingBuffer(std::unique_ptr<RingBuffer> buffer)
    : buffer_(internal::BlockingRingBufferImpl(std::move(buffer))) {}

zx_status_t BlockingRingBuffer::Create(VmoidRegistry* vmoid_registry, const size_t blocks,
                                       uint32_t block_size, const char* label,
                                       std::unique_ptr<BlockingRingBuffer>* out) {
  std::unique_ptr<RingBuffer> buffer;
  zx_status_t status = RingBuffer::Create(vmoid_registry, blocks, block_size, label, &buffer);
  if (status != ZX_OK) {
    return status;
  }
  *out = std::unique_ptr<BlockingRingBuffer>(new BlockingRingBuffer(std::move(buffer)));
  return ZX_OK;
}

BlockingRingBufferReservation::~BlockingRingBufferReservation() {
  if (!Reserved()) {
    return;
  }
  Reset();
  buffer_->Wake();
}

}  // namespace storage
