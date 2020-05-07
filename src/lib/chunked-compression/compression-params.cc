// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <src/lib/chunked-compression/chunked-archive.h>
#include <src/lib/chunked-compression/compression-params.h>
#include <zstd/zstd.h>

namespace chunked_compression {

namespace {
constexpr size_t kKiB = 1024;
}  // namespace

bool CompressionParams::IsValid() {
  return MinCompressionLevel() <= compression_level && compression_level <= MaxCompressionLevel() &&
         MinChunkSize() <= chunk_size && chunk_size % MinChunkSize() == 0;
}

size_t CompressionParams::ComputeOutputSizeLimit(size_t len) {
  if (len == 0) {
    return 0ul;
  }
  const size_t num_frames = HeaderWriter::NumFramesForDataSize(len, chunk_size);
  size_t size = HeaderWriter::MetadataSizeForNumFrames(num_frames);
  size += (ZSTD_compressBound(chunk_size) * num_frames);
  return size;
}

int CompressionParams::DefaultCompressionLevel() { return 3; }
int CompressionParams::MinCompressionLevel() { return ZSTD_minCLevel(); }
int CompressionParams::MaxCompressionLevel() { return ZSTD_maxCLevel(); }

size_t CompressionParams::ChunkSizeForInputSize(size_t len) {
  constexpr size_t first_guess = 32 * kKiB;
  if (len / first_guess > kChunkArchiveMaxFrames) {
    // For huge files, just max out the number of frames.
    size_t target_size_upper = len / kChunkArchiveMaxFrames;
    return fbl::round_up(target_size_upper, MinChunkSize());
  }
  return first_guess;
}

size_t CompressionParams::MinChunkSize() { return 8 * kKiB; }

}  // namespace chunked_compression
