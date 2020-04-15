// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_CHUNKED_COMPRESSION_CHUNKED_COMPRESSOR_H_
#define SRC_LIB_CHUNKED_COMPRESSION_CHUNKED_COMPRESSOR_H_

#include <memory>
#include <optional>

#include <fbl/array.h>
#include <fbl/function.h>
#include <fbl/macros.h>

#include "chunked-archive.h"
#include "status.h"

namespace chunked_compression {

struct CompressionParams {
 public:
  CompressionParams();
  ~CompressionParams() = default;

  // How aggressively to compress.
  // MinCompressionLevel() <= compression_level <= MaxCompressionLevel()
  int compression_level;

  // Size of chunks. Will be rounded up to a multiple of 4096 bytes.
  // MinChunkSize() <= chunk_size <= MaxChunkSize()
  size_t chunk_size;

  static int DefaultCompressionLevel();
  static int MinCompressionLevel();
  static int MaxCompressionLevel();

  // Estimates a good chunk size for the given input size.
  static size_t ChunkSizeForInputSize(size_t len);
  static size_t MinChunkSize();
  static size_t MaxChunkSize();
};

class ChunkedCompressor {
 public:
  ChunkedCompressor();
  explicit ChunkedCompressor(CompressionParams params);
  ~ChunkedCompressor();
  ChunkedCompressor(ChunkedCompressor&& o) = default;
  ChunkedCompressor& operator=(ChunkedCompressor&& o) = default;
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ChunkedCompressor);

  // Convenience method to do a one-shot compression of |data|, returning an allocated
  // buffer containing the compressed bytes.
  static Status CompressBytes(const void* data, size_t data_len,
                              fbl::Array<uint8_t>* compressed_data_out, size_t* bytes_written_out);

  // Returns the minimum size that a buffer must be to hold the result of compressing |len| bytes.
  size_t ComputeOutputSizeLimit(size_t len);

  // Reads from |data| and writes the compressed representation to |dst|.
  // |dst_len| must be at least |ComputeOutputSizeLimit(data_len)| bytes long.
  // Returns the number of compressed bytes written in |bytes_written_out|.
  Status Compress(const void* data, size_t data_len, void* dst, size_t dst_len,
                  size_t* bytes_written_out);

  // Registers |callback| to be invoked after each frame is complete.
  using ProgressFn =
      fbl::Function<void(size_t bytes_read, size_t bytes_total, size_t bytes_written)>;
  void SetProgressCallback(ProgressFn callback) { progress_callback_ = std::move(callback); }

 private:
  // Compress a single |len|-byte chunk of data. Returns the number of bytes written in
  // |bytes_written_out|.
  Status CompressChunk(const void* data, size_t len, void* dst, size_t dst_len,
                       size_t* bytes_written_out);

  std::optional<ProgressFn> progress_callback_;

  CompressionParams params_;

  struct CompressionContext;
  std::unique_ptr<CompressionContext> context_;
};

}  // namespace chunked_compression

#endif  // SRC_LIB_CHUNKED_COMPRESSION_CHUNKED_COMPRESSOR_H_
