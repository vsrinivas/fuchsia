// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <src/lib/chunked-compression/chunked-archive.h>
#include <src/lib/chunked-compression/compression-params.h>
#include <zxtest/zxtest.h>

namespace chunked_compression {

namespace {
constexpr size_t kKiB = 1024;
constexpr size_t kMiB = 1024 * 1024;
}  // namespace

TEST(CompressionParamsTest, ChunkSizeForInputSize) {
  constexpr size_t kTargetSize = 32 * kKiB;
  constexpr struct Params {
    size_t input_len;
    size_t expected_output_len;
  } kTestParams[] {
    {0,               32 * kKiB},
    {32735 * kKiB,    32 * kKiB},
    {32736 * kKiB,    32 * kKiB},
    // Everything up to 32376KiB should use 32KiB frames.
    // (This is the size of data for a full seek table with 32KiB frames.)
    // Above this, the algorithm tries to maximize the number of frames.
    {32736 * kKiB + 1, 40 * kKiB},
    {32 * kMiB,        40 * kKiB},
    {64 * kMiB,        72 * kKiB},
    {128 * kMiB,       136 * kKiB},
  };

  for (const auto& params : kTestParams) {
    printf("Test case: input len %lu\n", params.input_len);
    size_t chunk_size = CompressionParams::ChunkSizeForInputSize(params.input_len, kTargetSize);
    EXPECT_EQ(chunk_size, params.expected_output_len);
    EXPECT_LE(HeaderWriter::NumFramesForDataSize(params.input_len, chunk_size),
              kChunkArchiveMaxFrames);
  }
}

}  // namespace chunked_compression
