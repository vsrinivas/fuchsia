// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_CHUNKED_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_CHUNKED_H_

#include <lib/zx/status.h>
#include <zircon/types.h>

#include <memory>
#include <optional>

#include <blobfs/compression-settings.h>
#include <blobfs/format.h>
#include <src/lib/chunked-compression/chunked-decompressor.h>
#include <src/lib/chunked-compression/streaming-chunked-compressor.h>

#include "compressor.h"
#include "decompressor.h"
#include "seekable-decompressor.h"

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

  ////////////////////////////////////////
  // Compressor interface
  size_t Size() const final;
  zx_status_t Update(const void* input_data, size_t input_length) final;
  zx_status_t End() final;

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
// (//src/lib/chunked-compression).
class SeekableChunkedDecompressor : public SeekableDecompressor {
 public:
  SeekableChunkedDecompressor() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(SeekableChunkedDecompressor);

  static zx_status_t CreateDecompressor(const void* seek_table_buf, size_t seek_table_buf_sz,
                                        size_t uncompressed_size,
                                        std::unique_ptr<SeekableDecompressor>* out);

  // SeekableDecompressor implementation.
  zx_status_t DecompressRange(void* uncompressed_buf, size_t* uncompressed_size,
                              const void* compressed_buf, size_t max_compressed_size,
                              size_t offset) final;
  zx::status<CompressionMapping> MappingForDecompressedRange(size_t offset, size_t len) final;

 private:
  chunked_compression::SeekTable seek_table_;
  chunked_compression::ChunkedDecompressor decompressor_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_CHUNKED_H_
