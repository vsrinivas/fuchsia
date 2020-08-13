// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "compression/zstd-seekable.h"

// Maximum uncompressed buffer size.
constexpr size_t kMaxUncompressedBufSize = 10000;

// Basic fuzzer for BlobFS's internal zstd seekable decompression strategy. This fuzzer tests zstd
// seekable behaviour when an accurate archive size is provided, but the archive may be malformed.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);
  size_t uncompressed_size = provider.ConsumeIntegral<size_t>();
  size_t offset = provider.ConsumeIntegral<size_t>();
  const bool use_valid_params = provider.ConsumeBool();
  const size_t compressed_size = provider.remaining_bytes();

  uncompressed_size %= kMaxUncompressedBufSize + 1;
  if (use_valid_params) {
    // Use within-bounds `uncompressed_size` and `offset` assuming a compression factor of 2.
    size_t max_uncompressed_size = 2 * compressed_size;
    uncompressed_size %= max_uncompressed_size + 1;
    offset %= max_uncompressed_size - uncompressed_size + 1;
  }

  // Remaining data acts as compressed buf.
  std::vector<uint8_t> compressed_buf = provider.ConsumeRemainingBytes<uint8_t>();
  std::vector<uint8_t> uncompressed_buf(uncompressed_size);

  // Invoke decompression API.
  blobfs::ZSTDSeekableDecompressor decompressor;
  decompressor.DecompressArchive(uncompressed_buf.data(), &uncompressed_size, compressed_buf.data(),
                                 compressed_size, offset);

  return 0;
}
