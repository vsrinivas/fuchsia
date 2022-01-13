// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_CHUNKED_COMPRESSION_COMPRESSION_PARAMS_H_
#define SRC_LIB_CHUNKED_COMPRESSION_COMPRESSION_PARAMS_H_

#include <zircon/types.h>

namespace chunked_compression {

// CompressionParams describes the configuration for compression.
struct CompressionParams {
 public:
  // Validates the configured parameters.
  bool IsValid();

  // Returns the minimum size that a buffer must be to hold the result of compressing |len| bytes,
  // given the configured parameters.
  size_t ComputeOutputSizeLimit(size_t len);

  // How aggressively to compress.
  // MinCompressionLevel() <= compression_level <= MaxCompressionLevel()
  int compression_level = DefaultCompressionLevel();

  // Size of chunks. Will be rounded up to a multiple of 4096 bytes.
  // MinChunkSize() <= chunk_size <= MaxChunkSize()
  size_t chunk_size = MinChunkSize();

  // Whether to include a per-frame checksum.
  // Each frame is independently validated with its checksum when decompressed.
  bool frame_checksum = false;

  static int DefaultCompressionLevel();
  static int MinCompressionLevel();
  static int MaxCompressionLevel();

  // Estimates a good chunk size for the given input size.
  // |len| is the data input size.
  // |target_size| is the frame size to target. A frame size greater than or equal to this
  // will be returned (greater when the data is too large to support the target size.)
  static size_t ChunkSizeForInputSize(size_t len, size_t target_size);
  static size_t MinChunkSize();
};

}  // namespace chunked_compression

#endif  // SRC_LIB_CHUNKED_COMPRESSION_COMPRESSION_PARAMS_H_
