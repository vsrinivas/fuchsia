// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_CHUNKED_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_CHUNKED_H_

#include <lib/stdcompat/span.h>
#include <lib/zx/result.h>
#include <zircon/types.h>

#include <memory>
#include <optional>

#include <src/lib/chunked-compression/chunked-decompressor.h>
#include <src/lib/chunked-compression/streaming-chunked-compressor.h>

#include "src/lib/chunked-compression/chunked-archive.h"
#include "src/storage/blobfs/compression/compressor.h"
#include "src/storage/blobfs/compression/decompressor.h"
#include "src/storage/blobfs/compression/seekable_decompressor.h"
#include "src/storage/blobfs/compression_settings.h"
#include "src/storage/blobfs/format.h"

namespace blobfs {

// Implementation of |Compressor| backed by the "chunked-compression" library
// (//src/lib/chunked-compression).
class ChunkedCompressor : public Compressor {
 public:
  ChunkedCompressor() = delete;
  ~ChunkedCompressor() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(ChunkedCompressor);

  static uint32_t InodeHeaderCompressionFlags() { return kBlobFlagChunkCompressed; }

  static zx_status_t Create(CompressionSettings settings, size_t input_size,
                            size_t* output_limit_out, std::unique_ptr<ChunkedCompressor>* out);

  // Registers |dst| as the output for compression.
  // Must be called before |Update()| or |End()| are called.
  zx_status_t SetOutput(void* dst, size_t dst_len);

  // Returns an upper bound on the size of the buffer required to store the compressed
  // representation of a blob of size `input_length`.
  static size_t BufferMax(size_t input_length);

  // Compressor interface
  size_t Size() const final;
  zx_status_t Update(const void* input_data, size_t input_length) final;
  zx_status_t End() final;

  size_t GetChunkSize() const override { return compressor_.params().chunk_size; }

 private:
  explicit ChunkedCompressor(chunked_compression::StreamingChunkedCompressor compressor,
                             size_t input_len);

  chunked_compression::StreamingChunkedCompressor compressor_;
  size_t input_len_;
  // Set when End() is called to the final output size.
  std::optional<size_t> compressed_size_;
};

// Implementation of |Decompressor| backed by the "chunked-compression" library
// (//src/lib/chunked-compression).
class ChunkedDecompressor : public Decompressor {
 public:
  ChunkedDecompressor() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(ChunkedDecompressor);

  // Decompressor implementation.
  zx_status_t Decompress(void* uncompressed_buf, size_t* uncompressed_size,
                         const void* compressed_buf, const size_t max_compressed_size) final;

 private:
  chunked_compression::ChunkedDecompressor decompressor_;
};

// Implementation of |SeekableDecompressor| backed by the "chunked-compression" library
// (//src/lib/chunked-compression). Thread-safe
class SeekableChunkedDecompressor : public SeekableDecompressor {
 public:
  explicit SeekableChunkedDecompressor(std::unique_ptr<chunked_compression::SeekTable> seek_table);
  DISALLOW_COPY_ASSIGN_AND_MOVE(SeekableChunkedDecompressor);

  // |max_compressed_size| is used for validation purposes only.
  static zx_status_t CreateDecompressor(cpp20::span<const uint8_t> seek_table_data,
                                        size_t max_compressed_size,
                                        std::unique_ptr<SeekableDecompressor>* out);

  // Helper function to calculate a CompressionMapping from a given |seek_table|.
  static zx::result<CompressionMapping> MappingForDecompressedRange(
      const chunked_compression::SeekTable& seek_table, size_t offset, size_t len,
      size_t max_decompressed_len);

  // SeekableDecompressor implementation.

  zx_status_t DecompressRange(void* uncompressed_buf, size_t* uncompressed_size,
                              const void* compressed_buf, size_t max_compressed_size,
                              size_t offset) final;

  zx::result<CompressionMapping> MappingForDecompressedRange(
      size_t offset, size_t len, size_t max_decompressed_len) const final;

  CompressionAlgorithm algorithm() const final { return CompressionAlgorithm::kChunked; }

 private:
  const std::unique_ptr<chunked_compression::SeekTable> seek_table_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_CHUNKED_H_
