// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/chunked-compression/compression-params.h>
#include <zstd/zstd.h>

namespace chunked_compression {

bool CompressionParams::IsValid() {
  return MinCompressionLevel() <= compression_level && compression_level <= MaxCompressionLevel() &&
         MinChunkSize() <= chunk_size && chunk_size <= MaxChunkSize();
}

int CompressionParams::DefaultCompressionLevel() { return 3; }
int CompressionParams::MinCompressionLevel() { return ZSTD_minCLevel(); }
int CompressionParams::MaxCompressionLevel() { return ZSTD_maxCLevel(); }

size_t CompressionParams::ChunkSizeForInputSize(size_t len) {
  if (len <= (1 << 20)) {  // Up to 1M
    return MinChunkSize();
  } else if (len <= (1 << 24)) {  // Up to 16M
    return 262144;                // 256K, or 64 4k pages
  } else if (len <= (1 << 26)) {  // Up to 64M
    return 524288;                // 512K, or 128 4k pages
  } else {
    return MaxChunkSize();
  }
}
size_t CompressionParams::MinChunkSize() { return 131072; /* 128K, or 32 4k pages */ }
size_t CompressionParams::MaxChunkSize() { return 1048576; /* 1M, or 256 4k pages */ }

}  // namespace chunked_compression
