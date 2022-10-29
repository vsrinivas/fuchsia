// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/block_client/cpp/writer.h"

#include <safemath/checked_math.h>

namespace block_client {

zx_status_t Writer::Write(uint64_t offset, const size_t count, void* buf) {
  constexpr uint64_t kMinWriteSize = 128 * 1024;
  const uint64_t write_size = std::max(kMinWriteSize, block_size_);

  if (block_size_ == 0) {
    if (zx_status_t status = buffer_.CreateAndMap(write_size, "block_client::Writer");
        status != ZX_OK) {
      return status;
    }

    if (zx_status_t status = device_.BlockAttachVmo(buffer_.vmo(), &vmoid_.GetReference(&device_));
        status != ZX_OK) {
      return status;
    }

    fuchsia_hardware_block::wire::BlockInfo info;
    if (zx_status_t status = device_.BlockGetInfo(&info); status != ZX_OK)
      return status;

    block_size_ = info.block_size;
  }

  if (count % block_size_ != 0 || offset % block_size_ != 0)
    return ZX_ERR_INVALID_ARGS;

  uint64_t remaining = count;
  while (remaining > 0) {
    size_t amount = std::min(remaining, write_size);
    memcpy(buffer_.start(), buf, amount);
    buf = static_cast<uint8_t*>(buf) + amount;
    block_fifo_request_t request = {
        .opcode = BLOCKIO_WRITE,
        .vmoid = vmoid_.get(),
        .length = safemath::checked_cast<uint32_t>(amount / block_size_),
        .vmo_offset = 0,
        .dev_offset = safemath::checked_cast<uint32_t>(offset / block_size_),
    };
    if (zx_status_t status = device_.FifoTransaction(&request, 1); status != ZX_OK)
      return status;
    remaining -= amount;
    offset += amount;
  }

  return ZX_OK;
}

}  // namespace block_client
