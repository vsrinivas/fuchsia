// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_DECOMPRESSOR_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_DECOMPRESSOR_H_

#include <stddef.h>
#include <zircon/types.h>

#include <memory>

#include <blobfs/compression-settings.h>
#include <fbl/macros.h>

namespace blobfs {

// A `Decompressor` is used to decompress whole blobs transparently. See `Compressor` documentation
// for properties of `Compressor`/`Decompressor` pair implementations.
class Decompressor {
 public:
  static zx_status_t Create(CompressionAlgorithm algorithm, std::unique_ptr<Decompressor>* out);

  Decompressor() = default;
  virtual ~Decompressor() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(Decompressor);

  // Decompresses data archive from buffer, `compressed_buf`, which has size `max_compressed_size`.
  // The actual archive contents is at most `max_compressed_size`, but may be smaller. Decompressed
  // data is written to `uncompressed_buf`, which has a size of `*uncompressed_size`. If the return
  // value is `ZX_OK, then the number of bytes written is written to `uncompressed_buf` is stored in
  // `*uncompressed_size`.
  virtual zx_status_t Decompress(void* uncompressed_buf, size_t* uncompressed_size,
                                 const void* compressed_buf, const size_t max_compressed_size) = 0;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_DECOMPRESSOR_H_
