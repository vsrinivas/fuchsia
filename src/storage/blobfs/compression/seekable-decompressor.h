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
  //
  // The concrete implementation is free to return an arbitrarily large range of bytes, but
  // [offset, offset+len) will always be contained in the mapping.
  virtual zx::status<CompressionMapping> MappingForDecompressedRange(size_t offset, size_t len) = 0;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_SEEKABLE_DECOMPRESSOR_H_
