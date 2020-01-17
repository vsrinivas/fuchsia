// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_COMPRESSOR_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_COMPRESSOR_H_

#include <zircon/types.h>

#include <fbl/macros.h>
#include <lz4/lz4frame.h>
#include <zstd/zstd.h>

namespace blobfs {

enum class CompressionAlgorithm {
  LZ4,
  ZSTD,
  ZSTD_SEEKABLE,
};

// A Compressor is used to compress data transparently before it is written
// back to disk.
class Compressor {
 public:
  Compressor() = default;
  virtual ~Compressor() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(Compressor);

  // Returns the compressed size of the data so far. Simply starting initialization
  // may result in a nonzero |Size()|.
  virtual size_t Size() const = 0;

  // Continues the compression after initialization.
  virtual zx_status_t Update(const void* input_data, size_t input_length) = 0;

  // Finishes the compression process.
  // Must be called before compression is considered complete.
  virtual zx_status_t End() = 0;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_COMPRESSOR_H_
