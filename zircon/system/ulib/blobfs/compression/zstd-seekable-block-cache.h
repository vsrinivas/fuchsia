// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_BLOCK_CACHE_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_BLOCK_CACHE_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include <memory>
#include <vector>

#include <blobfs/format.h>
#include <fbl/macros.h>

namespace blobfs {

// Interface for selectively retaining blocks from a blob. Offsets over this interface refer to
// *data block offsets*. Data block refers to blocks of encoded file contents (i.e., not merkle
// blocks). Offsets are relative to the beginning of said file content.
class ZSTDSeekableBlockCache {
 public:
  ZSTDSeekableBlockCache() = default;
  virtual ~ZSTDSeekableBlockCache() = default;
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ZSTDSeekableBlockCache);

  // Attempt to cache the block pointed to by `data`, which refers `kBlobfsBlockSize` bytes of data
  // at logical data block offset `data_block_offset`. If the cache stores the block, it will create
  // a copy of `data` internally. The return value indicates whether or not the cache is in a
  // consistent state.
  virtual zx_status_t WriteBlock(uint8_t* buf, uint32_t data_block_offset) = 0;

  // Attempt to read the block at logical data block offset `data_block_offset` into the first
  // `kBlobfsBlockSize` bytes of `buf`. The read succeeded if and only if `ZX_OK` is returned.
  // Otherwise, `buf` may be in an inconsistent state.
  virtual zx_status_t ReadBlock(uint8_t* buf, uint32_t data_block_offset) = 0;
};

class ZSTDSeekableProxyBlockCache : public ZSTDSeekableBlockCache {
 public:
  ZSTDSeekableProxyBlockCache() = default;
  explicit ZSTDSeekableProxyBlockCache(std::unique_ptr<ZSTDSeekableBlockCache> delegate);
  virtual ~ZSTDSeekableProxyBlockCache() = default;
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ZSTDSeekableProxyBlockCache);

  virtual zx_status_t WriteBlock(uint8_t* buf, uint32_t data_block_offset);
  virtual zx_status_t ReadBlock(uint8_t* buf, uint32_t data_block_offset);

 protected:
  std::unique_ptr<ZSTDSeekableBlockCache> delegate_;
};

class ZSTDSeekableSingleBlockCache : public ZSTDSeekableProxyBlockCache {
 public:
  ZSTDSeekableSingleBlockCache() = default;
  explicit ZSTDSeekableSingleBlockCache(std::unique_ptr<ZSTDSeekableBlockCache> delegate);
  virtual ~ZSTDSeekableSingleBlockCache() = default;
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ZSTDSeekableSingleBlockCache);

  virtual zx_status_t WriteBlock(uint8_t* buf, uint32_t data_block_offset);
  virtual zx_status_t ReadBlock(uint8_t* buf, uint32_t data_block_offset);

 protected:
  std::unique_ptr<std::vector<uint8_t>> block_ = nullptr;
};

class ZSTDSeekableMostRecentBlockCache : public ZSTDSeekableSingleBlockCache {
 public:
  explicit ZSTDSeekableMostRecentBlockCache(std::unique_ptr<ZSTDSeekableBlockCache> delegate);
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ZSTDSeekableMostRecentBlockCache);

  zx_status_t WriteBlock(uint8_t* buf, uint32_t data_block_offset) final;
  zx_status_t ReadBlock(uint8_t* buf, uint32_t data_block_offset) final;

 private:
  uint32_t data_block_offset_;
};

class ZSTDSeekableFirstBlockCache : public ZSTDSeekableSingleBlockCache {
 public:
  explicit ZSTDSeekableFirstBlockCache(std::unique_ptr<ZSTDSeekableBlockCache> delegate);
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ZSTDSeekableFirstBlockCache);

  zx_status_t WriteBlock(uint8_t* buf, uint32_t data_block_offset) final;
  zx_status_t ReadBlock(uint8_t* buf, uint32_t data_block_offset) final;
};

class ZSTDSeekableLastBlockCache : public ZSTDSeekableSingleBlockCache {
 public:
  explicit ZSTDSeekableLastBlockCache(uint32_t num_data_blocks);
  ZSTDSeekableLastBlockCache(uint32_t num_data_blocks, std::unique_ptr<ZSTDSeekableBlockCache> delegate);
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ZSTDSeekableLastBlockCache);

  zx_status_t WriteBlock(uint8_t* buf, uint32_t data_block_offset) final;
  zx_status_t ReadBlock(uint8_t* buf, uint32_t data_block_offset) final;

 private:
  uint32_t num_data_blocks_;
};

class ZSTDSeekableDefaultBlockCache : public ZSTDSeekableProxyBlockCache {
 public:
  explicit ZSTDSeekableDefaultBlockCache(uint32_t num_data_blocks);
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ZSTDSeekableDefaultBlockCache);

  // TODO: This is just for an experiment.
  zx_status_t ReadBlock(uint8_t* buf, uint32_t data_block_offset) final;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_BLOCK_CACHE_H_
