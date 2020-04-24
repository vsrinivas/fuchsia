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
constexpr size_t kGiB = 1024 * 1024 * 1024;
}  // namespace

TEST(CompressionParamsTest, ChunkSizeForInputSize) {
  constexpr struct Params {
    size_t input_len;
    size_t expected_output_len;
  } kTestParams[] {
    {0,               8 * kKiB},
    {4 * kMiB,        8 * kKiB},
    {4 * kMiB + 1,    16 * kKiB},
    {8 * kMiB + 1,    32 * kKiB},
    {16 * kMiB + 1,   64 * kKiB},
    {32 * kMiB + 1,   128 * kKiB},
    {64 * kMiB + 1,   256 * kKiB},
    {128 * kMiB + 1,  512 * kKiB},
    {256 * kMiB + 1,  kMiB},
    {512 * kMiB + 1,  2 * kMiB},
    {kGiB + 1,        4 * kMiB},
    {2 * kGiB + 1,    8 * kMiB},
    {4 * kGiB,        16 * kMiB},
    // For large files, the algorithm changes to try to fill up the entire seek table.
    // Since the seek table contains an odd number of entries, these result in unusual chunk sizes,
    // but they are always divisible by MinChunkSize().
    {4 * kGiB + 1,    4104 * kKiB},
    {8 * kGiB + 1,    8208 * kKiB},
    {16 * kGiB + 1,   16408 * kKiB},
  };

  for (const auto& params : kTestParams) {
    size_t chunk_len = CompressionParams::ChunkSizeForInputSize(params.input_len);
    EXPECT_EQ(chunk_len, params.expected_output_len);
    EXPECT_LE(HeaderWriter::NumFramesForDataSize(params.input_len, chunk_len),
              kChunkArchiveMaxFrames);
  }
}

}  // namespace chunked_compression
