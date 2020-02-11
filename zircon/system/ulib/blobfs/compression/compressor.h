// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_COMPRESSOR_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_COMPRESSOR_H_

#include <zircon/types.h>

#include <memory>

#include <fbl/macros.h>

namespace blobfs {

enum class CompressionAlgorithm {
  LZ4,
  ZSTD,
  ZSTD_SEEKABLE,
};

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
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_COMPRESSOR_H_
