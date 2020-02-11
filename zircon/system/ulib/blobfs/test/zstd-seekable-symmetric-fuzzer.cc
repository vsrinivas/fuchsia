// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "compression/zstd-seekable.h"

// Compression/decompression symmetry fuzzer for zstd seekable. The fuzzer compresses and then
// decompresses part of a seekable zstd archive. This fuzzer uses its input to:
// 1. Select the size of the read (in uncompressed space) during decompression;
// 2. Select the offset for the read (in uncompressed space) during decompression;
// 3. Determine the contents of the archive (in incumpressed space) prior to compression.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);
  size_t uncompressed_size = provider.ConsumeIntegral<size_t>();
  size_t offset = provider.ConsumeIntegral<size_t>();
  const size_t data_size = provider.remaining_bytes();
  std::vector<uint8_t> src_buf = provider.ConsumeRemainingBytes<uint8_t>();
  const size_t max_compressed_size = blobfs::ZSTDSeekableCompressor::BufferMax(data_size);
  std::vector<uint8_t> compressed_buf(max_compressed_size);

  // Compress data.
  std::unique_ptr<blobfs::ZSTDSeekableCompressor> compressor;
  assert(blobfs::ZSTDSeekableCompressor::Create(data_size, compressed_buf.data(),
                                                max_compressed_size, &compressor) == ZX_OK);
  assert(compressor->Update(data, data_size) == ZX_OK);
  assert(compressor->End() == ZX_OK);

  // Select size and offset for decompression.
  uncompressed_size %= data_size + 1;
  offset %= data_size - uncompressed_size + 1;

  // Store initial `uncompressed_size` to check for complete read.
  const size_t initial_uncompressed_size = uncompressed_size;

  // Decompress from uncompressed space `offset` through `offset + uncompressed_size` into
  // `uncompressed_buf`.
  std::vector<uint8_t> uncompressed_buf(initial_uncompressed_size);
  blobfs::ZSTDSeekableDecompressor decompressor;
  assert(decompressor.DecompressRange(uncompressed_buf.data(), &uncompressed_size,
                                      compressed_buf.data(), compressor->Size(), offset) == 0);

  // Verify correctness of read.
  assert(std::memcmp(data + offset, uncompressed_buf.data(), uncompressed_size) == 0);

  // Verify size of read.
  assert(initial_uncompressed_size == uncompressed_size);

  return 0;
}
