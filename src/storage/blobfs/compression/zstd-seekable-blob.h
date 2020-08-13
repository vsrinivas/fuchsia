// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_BLOB_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_BLOB_H_

#include <lib/fzl/owned-vmo-mapper.h>
#include <stdint.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/macros.h>
#include <zstd/zstd_seekable.h>

#include "zstd-compressed-block-collection.h"
#include "zstd-seekable.h"

namespace blobfs {

// RandomAccessCompressedBlob is an interface for reading contiguous *uncompressed data* from a
// compressed blob archive. Offsets in this API are in bytes relative to the start of the compressed
// archive (i.e., exclude merkle blocks and any BlobFS-managed archive header.)
//
// This interface is separated from the concrete implementation below to make testing easier.
class RandomAccessCompressedBlob {
 public:
  RandomAccessCompressedBlob() = default;
  virtual ~RandomAccessCompressedBlob() = default;

  // Load into |buf| exactly |num_bytes| bytes starting at _uncompressed_ file contents byte offset
  // |data_byte_offset|. The value of data in |buf| is expected to be valid if and only if the
  // return value is |ZX_OK|.
  virtual zx_status_t Read(uint8_t* buf, uint64_t data_byte_offset, uint64_t num_bytes) = 0;
};

// ZSTDSeekableBlob as an implementation of |RandomAccessCompressedBlob| that uses the ZSTD Seekable
// Format.
//
// https://github.com/facebook/zstd/blob/dev/contrib/seekable_format/zstd_seekable_compression_format.md.
class ZSTDSeekableBlob : public RandomAccessCompressedBlob {
 public:
  // Create a |ZSTDSeekableBlob|. It is the invoker's responsibility to ensure that the VMO
  // populated on |compressed_block_collection.Read()| corresponds to |mapped_vmo|.
  static zx_status_t Create(
      fzl::OwnedVmoMapper* mapped_vmo,
      std::unique_ptr<ZSTDCompressedBlockCollection> compressed_block_collection,
      std::unique_ptr<ZSTDSeekableBlob>* out);

  // RandomAccessCompressedBlob implementation.
  zx_status_t Read(uint8_t* buf, uint64_t data_byte_offset, uint64_t num_bytes) final;

  const uint8_t* decompressed_data_start() const {
    return static_cast<uint8_t*>(mapped_vmo_->start());
  }

 private:
  ZSTDSeekableBlob(fzl::OwnedVmoMapper* mapped_vmo,
                   std::unique_ptr<ZSTDCompressedBlockCollection> compressed_block_collection);

  zx_status_t ReadHeader();

  ZSTDSeekableHeader header_;
  fzl::OwnedVmoMapper* mapped_vmo_;
  std::unique_ptr<ZSTDCompressedBlockCollection> compressed_block_collection_;
};

// Type used for opaque pointer in ZSTD Seekable custom |ZSTD_seekable_seek| and
// |ZSTD_seekable_read| API.
struct ZSTDSeekableFile {
  ZSTDSeekableBlob* blob;
  ZSTDCompressedBlockCollection* blocks;
  unsigned long long byte_offset;
  unsigned long long num_bytes;
  zx_status_t status;
};

// ZSTD Seekable custom |ZSTD_seekable_seek| and |ZSTD_seekable_read| API.
int ZSTDSeek(void* void_ptr_zstd_seekable_file, long long byte_offset, int origin);
int ZSTDRead(void* void_ptr_zstd_seekable_file, void* buf, size_t num_bytes);

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_BLOB_H_
