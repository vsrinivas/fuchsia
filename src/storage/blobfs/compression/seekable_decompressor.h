// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_SEEKABLE_DECOMPRESSOR_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_SEEKABLE_DECOMPRESSOR_H_

#include <lib/zx/status.h>
#include <stddef.h>
#include <zircon/types.h>

#include <memory>
#include <optional>

#include <fbl/macros.h>

#include "src/storage/blobfs/compression_settings.h"

namespace blobfs {

// CompressionMapping describes the mapping between a range of bytes in a compressed file and the
// range they decompress to.
struct CompressionMapping {
  size_t compressed_offset;
  size_t compressed_length;
  size_t decompressed_offset;
  size_t decompressed_length;
};

// A `SeekableDecompressor` is used to decompress parts of blobs transparently. See `Compressor`
// documentation for properties of `Compressor`/`SeekableDecompressor` pair implementations.
// Implementations must be thread-safe.
class SeekableDecompressor {
 public:
  SeekableDecompressor() = default;
  virtual ~SeekableDecompressor() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(SeekableDecompressor);

  // Decompresses data archive from buffer, `compressed_buf`, which has size `max_compressed_size`,
  // starting at _uncompressed_ byte offset, `offset`. Decompress at most `uncompressed_size` bytes.
  // The actual archive contents is at most `max_compressed_size`, but may be smaller. Decompressed
  // data is written to `uncompressed_buf`, which has a size of `*uncompressed_size`. If the return
  // value is `ZX_OK, then the number of bytes written is written to `uncompressed_buf` is stored in
  // `*uncompressed_size`.
  virtual zx_status_t DecompressRange(void* uncompressed_buf, size_t* uncompressed_size,
                                      const void* compressed_buf, size_t max_compressed_size,
                                      size_t offset) = 0;

  // Looks up the range [offset, offset+len) in the decompressed space, and returns a mapping which
  // describes the range of bytes to decompress which will contain the target range.
  // `max_decompressed_len` is the maximum length the returned decompressed range will span, and
  // must be greater than zero.
  //
  // The concrete implementation is free to return an arbitrarily large range of bytes as long as it
  // is less than or equal to `max_decompressed_len`. The returned decompressed range is guaranteed
  // to contain `offset` but its length might be less than `len` if it was trimmed to a smaller
  // `max_decompressed_len`.
  virtual zx::result<CompressionMapping> MappingForDecompressedRange(
      size_t offset, size_t len, size_t max_decompressed_len) const = 0;

  // Returns the CompressionAlgorithm that this SeekableDecompressor supports.
  virtual CompressionAlgorithm algorithm() const = 0;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_SEEKABLE_DECOMPRESSOR_H_
