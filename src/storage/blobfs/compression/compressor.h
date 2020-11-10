// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_COMPRESSOR_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_COMPRESSOR_H_

#include <zircon/types.h>

#include <memory>

#include <blobfs/compression-settings.h>
#include <fbl/macros.h>

namespace blobfs {

// A `Compressor` is used to compress whole blobs transparently. Note that compressors may add
// metadata beyond the underlying compression archive format so long as the corresponding
// `Decompressor` correctly interprets the metadata and archive. Addition of metadata should not
// break the symmetry of `Compressor`/`Decompressor` or `Compressor`/`SeekableDecompressor` pairs.
// Informally:
//
//     alpha_decompressor.Decompress(alpha_compressor.Compress(data)) == data
//
// and
//
//     alpha_seekable_decompressor.Decompress(alpha_compressor.Compress(data), len, offset)
//         == data[offset : offset + len)
//
// assuming 0 <= offset < length(data), 0 <= len, offset + len <= length(data). The `Compressor`,
// `Decompressor`, and `SeekableDecompressor` APIs actually operate over pairs of buffers. See API
// method documentation for details.
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

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_COMPRESSOR_H_
