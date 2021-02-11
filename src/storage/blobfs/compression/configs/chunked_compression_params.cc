// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/compression/configs/chunked_compression_params.h"

#include "src/lib/chunked-compression/chunked-compressor.h"

namespace blobfs {

namespace {
using ::chunked_compression::CompressionParams;

constexpr int kDefaultLevel = 14;
constexpr int kTargetFrameSize = 32 * 1024;
}  // namespace

CompressionParams GetDefaultChunkedCompressionParams(const size_t input_size) {
  CompressionParams params;
  params.compression_level = kDefaultLevel;
  params.chunk_size = CompressionParams::ChunkSizeForInputSize(input_size, kTargetFrameSize);
  return params;
}

}  // namespace blobfs
