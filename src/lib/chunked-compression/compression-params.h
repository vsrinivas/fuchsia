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
  CompressionParams() = default;
  ~CompressionParams() = default;

  // Validates the configured parameters.
  bool IsValid();

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
  static size_t ChunkSizeForInputSize(size_t len);
  static size_t MinChunkSize();
  static size_t MaxChunkSize();
};

}  // namespace chunked_compression

#endif  // SRC_LIB_CHUNKED_COMPRESSION_COMPRESSION_PARAMS_H_
