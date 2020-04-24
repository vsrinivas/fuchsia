// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <fbl/array.h>
#include <src/lib/chunked-compression/chunked-compressor.h>
#include <src/lib/chunked-compression/chunked-decompressor.h>

namespace {

using chunked_compression::ChunkedCompressor;
using chunked_compression::ChunkedDecompressor;
using chunked_compression::kStatusOk;
using chunked_compression::Status;

}  // namespace

// Fuzz test which compresses and then decompresses |data|.
// Aborts if the result isn't the same as the provided input.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  fbl::Array<uint8_t> compressed_buf;
  size_t compressed_size;
  Status status = ChunkedCompressor::CompressBytes(data, size, &compressed_buf, &compressed_size);
  if (status != kStatusOk) {
    fprintf(stderr, "Failed to compress: %d\n", status);
    return 0;
  }

  fbl::Array<uint8_t> decompressed_buf;
  size_t decompressed_size;
  status = ChunkedDecompressor::DecompressBytes(compressed_buf.get(), compressed_size,
                                                &decompressed_buf, &decompressed_size);
  if (status != kStatusOk) {
    fprintf(stderr, "Failed to decompress: %d\n", status);
    return 0;
  }

  ZX_ASSERT(decompressed_size == size);
  ZX_ASSERT(memcmp(decompressed_buf.get(), data, size) == 0);
  return 0;
}
