// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zstd-seekable-block-cache.h"

#include <memory>
#include <vector>

#include <stdint.h>
#include <string.h>

#include <zircon/status.h>
#include <zircon/errors.h>

// TODO: This is just for an experiment.
#include <fs/trace.h>

namespace blobfs {

ZSTDSeekableProxyBlockCache::ZSTDSeekableProxyBlockCache(std::unique_ptr<ZSTDSeekableBlockCache> delegate)
 : delegate_(std::move(delegate)) {}

zx_status_t ZSTDSeekableProxyBlockCache::WriteBlock(uint8_t* buf, uint32_t data_block_offset) {
  if (delegate_ == nullptr) {
    return ZX_OK;
  }

  return delegate_->WriteBlock(buf, data_block_offset);
}

zx_status_t ZSTDSeekableProxyBlockCache::ReadBlock(uint8_t* buf, uint32_t data_block_offset) {
  if (delegate_ == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }

  return delegate_->ReadBlock(buf, data_block_offset);
}

ZSTDSeekableSingleBlockCache::ZSTDSeekableSingleBlockCache(std::unique_ptr<ZSTDSeekableBlockCache> delegate)
: ZSTDSeekableProxyBlockCache(std::move(delegate)) {}

zx_status_t ZSTDSeekableSingleBlockCache::WriteBlock(uint8_t* buf, uint32_t data_block_offset) {
  if (block_ == nullptr) {
    block_ = std::make_unique<std::vector<uint8_t>>(kBlobfsBlockSize);
  }

  memcpy(block_->data(), buf, kBlobfsBlockSize);
  return ZX_OK;
}

zx_status_t ZSTDSeekableSingleBlockCache::ReadBlock(uint8_t* buf, uint32_t data_block_offset) {
  if (block_ == nullptr) {
    return ZSTDSeekableProxyBlockCache::ReadBlock(buf, data_block_offset);
  }

  memcpy(buf, block_->data(), kBlobfsBlockSize);
  return ZX_OK;
}

ZSTDSeekableMostRecentBlockCache::ZSTDSeekableMostRecentBlockCache(std::unique_ptr<ZSTDSeekableBlockCache> delegate)
  : ZSTDSeekableSingleBlockCache(std::move(delegate)) {}

zx_status_t ZSTDSeekableMostRecentBlockCache::WriteBlock(uint8_t* buf, uint32_t data_block_offset) {
  data_block_offset_ = data_block_offset;
  return ZSTDSeekableSingleBlockCache::WriteBlock(buf, data_block_offset);
}

zx_status_t ZSTDSeekableMostRecentBlockCache::ReadBlock(uint8_t* buf, uint32_t data_block_offset) {
  if (data_block_offset == data_block_offset_) {
    return ZSTDSeekableSingleBlockCache::ReadBlock(buf, data_block_offset);
  }

  // Consult delegate (if any).
  return ZSTDSeekableProxyBlockCache::ReadBlock(buf, data_block_offset);
}

ZSTDSeekableFirstBlockCache::ZSTDSeekableFirstBlockCache(std::unique_ptr<ZSTDSeekableBlockCache> delegate)
  : ZSTDSeekableSingleBlockCache(std::move(delegate)) {}

zx_status_t ZSTDSeekableFirstBlockCache::WriteBlock(uint8_t* buf, uint32_t data_block_offset) {
  if (data_block_offset == 0) {
    // Block at fixed offset cannot change because blob is read-only. If already cached, don't
    // bother caching again.
    if (block_ != nullptr) {
      return ZX_OK;
    }

    return ZSTDSeekableSingleBlockCache::WriteBlock(buf, data_block_offset);
  }

  // Consult delegate (if any).
  return ZSTDSeekableProxyBlockCache::WriteBlock(buf, data_block_offset);
}

zx_status_t ZSTDSeekableFirstBlockCache::ReadBlock(uint8_t* buf, uint32_t data_block_offset) {
  if (data_block_offset == 0) {
    return ZSTDSeekableSingleBlockCache::ReadBlock(buf, data_block_offset);
  }

  // Consult delegate (if any).
  return ZSTDSeekableProxyBlockCache::ReadBlock(buf, data_block_offset);
}

ZSTDSeekableLastBlockCache::ZSTDSeekableLastBlockCache(uint32_t num_data_blocks) : ZSTDSeekableSingleBlockCache(nullptr), num_data_blocks_(num_data_blocks) {}

ZSTDSeekableLastBlockCache::ZSTDSeekableLastBlockCache(uint32_t num_data_blocks, std::unique_ptr<ZSTDSeekableBlockCache> delegate) : ZSTDSeekableSingleBlockCache(std::move(delegate)), num_data_blocks_(num_data_blocks) {}

zx_status_t ZSTDSeekableLastBlockCache::WriteBlock(uint8_t* buf, uint32_t data_block_offset) {
  if (data_block_offset == num_data_blocks_ - 1) {
    // Block at fixed offset cannot change because blob is read-only. If already cached, don't
    // bother caching again.
    if (block_ != nullptr) {
      return ZX_OK;
    }

    return ZSTDSeekableSingleBlockCache::WriteBlock(buf, data_block_offset);
  }

  // Consult delegate (if any).
  return ZSTDSeekableProxyBlockCache::WriteBlock(buf, data_block_offset);
}

zx_status_t ZSTDSeekableLastBlockCache::ReadBlock(uint8_t* buf, uint32_t data_block_offset) {
  if (data_block_offset == num_data_blocks_ - 1) {
    return ZSTDSeekableSingleBlockCache::ReadBlock(buf, data_block_offset);
  }

  // Consult delegate (if any).
  return ZSTDSeekableProxyBlockCache::ReadBlock(buf, data_block_offset);
}

// Default caching strategy:
// Last block of blob, or else return first block of blob, or else return most recently read block
// of blob.
ZSTDSeekableDefaultBlockCache::ZSTDSeekableDefaultBlockCache(uint32_t num_data_blocks)
 : ZSTDSeekableProxyBlockCache(std::make_unique<ZSTDSeekableLastBlockCache>(num_data_blocks,
 std::make_unique<ZSTDSeekableMostRecentBlockCache>(std::make_unique<ZSTDSeekableSingleBlockCache>()))) {}

// TODO: This is just for an experiment.
zx_status_t ZSTDSeekableDefaultBlockCache::ReadBlock(uint8_t* buf, uint32_t data_block_offset) {
  return ZSTDSeekableProxyBlockCache::ReadBlock(buf, data_block_offset);
}

}  // namespace blobfs
