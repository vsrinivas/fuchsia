// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_BLOB_CACHE_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_BLOB_CACHE_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include <list>
#include <memory>

#include <blobfs/format.h>
#include <fbl/macros.h>

#include "zstd-seekable-blob.h"

namespace blobfs {

// TODO(markdittmer): Copy pasta from ZSTDSeekableBlockCollection.
//
// Interface for selectively retaining blocks from a blob. Offsets over this interface refer to
// *data block offsets*. Data block refers to blocks of encoded file contents (i.e., not merkle
// blocks). Offsets are relative to the beginning of said file content.
class ZSTDSeekableBlobCache {
 public:
  ZSTDSeekableBlobCache() = default;
  virtual ~ZSTDSeekableBlobCache() = default;
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ZSTDSeekableBlobCache);

  // TODO(markdittmer): Copy pasta from ZSTDSeekableBlockCollection.
  //
  // Attempt to cache the block pointed to by `data`, which refers `kBlobfsBlockSize` bytes of data
  // at logical data block offset `data_block_offset`. If the cache stores the block, it will create
  // a copy of `data` internally. The return value indicates whether or not the cache is in a
  // consistent state.
  virtual const std::unique_ptr<ZSTDSeekableBlob>& WriteBlob(std::unique_ptr<ZSTDSeekableBlob> blob) = 0;

  // TODO(markdittmer): Copy pasta from ZSTDSeekableBlockCollection.
  //
  // Attempt to read the block at logical data block offset `data_block_offset` into the first
  // `kBlobfsBlockSize` bytes of `buf`. The read succeeded if and only if `ZX_OK` is returned.
  // Otherwise, `buf` may be in an inconsistent state.
  virtual const std::unique_ptr<ZSTDSeekableBlob>& ReadBlob(uint32_t node_index) = 0;
};

class ZSTDSeekableLRUBlobCache : public ZSTDSeekableBlobCache {
 public:
  explicit ZSTDSeekableLRUBlobCache(size_t max_size);
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ZSTDSeekableLRUBlobCache);

  const std::unique_ptr<ZSTDSeekableBlob>& WriteBlob(std::unique_ptr<ZSTDSeekableBlob> blob) final;
  const std::unique_ptr<ZSTDSeekableBlob>& ReadBlob(uint32_t node_index) final;

 private:
  std::list<std::unique_ptr<ZSTDSeekableBlob>> blobs_;
  size_t max_size_;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_BLOB_CACHE_H_
