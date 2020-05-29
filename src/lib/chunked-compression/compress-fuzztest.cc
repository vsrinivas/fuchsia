// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <fbl/array.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <src/lib/chunked-compression/chunked-compressor.h>
#include <src/lib/chunked-compression/compression-params.h>

namespace {

using chunked_compression::ChunkedCompressor;
using chunked_compression::CompressionParams;
using chunked_compression::kStatusOk;
using chunked_compression::Status;

}  // namespace

// Fuzz test which compresses |data|, adjusting the parameters for compression based on the
// mutated input.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider fuzzed_data(data, size);
  int level = fuzzed_data.ConsumeIntegralInRange<int>(CompressionParams::MinCompressionLevel(),
                                                      CompressionParams::MaxCompressionLevel());
  auto remaining_data = fuzzed_data.ConsumeRemainingBytes<char>();

  CompressionParams params;
  params.compression_level = level;
  ChunkedCompressor compressor(params);

  size_t output_limit = compressor.ComputeOutputSizeLimit(remaining_data.size());
  fbl::Array<uint8_t> out_buf(new uint8_t[output_limit], output_limit);

  size_t compressed_size;
  Status status = compressor.Compress(remaining_data.data(), remaining_data.size(), out_buf.data(),
                                      out_buf.size(), &compressed_size);
  if (status != kStatusOk) {
    fprintf(stderr, "Failed to compress: %d\n", status);
  }

  return 0;
}
