// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compression.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "testdata/compression_test_data.h"

namespace {

// Runs the common decompression success case.
//
// input: compressed input data.
// size: input size.
// output: filled with the decompressed output as a string.
// loop_count: if provided, filled with the number of decompression loops ran.
//
// Should be called with ASSERT_NO_FATAL_FAILURES().
void Decompress(const uint8_t* input, size_t size, std::string* output, int* loop_count = nullptr) {
  ASSERT_TRUE(decompress_start(input, size));

  output->clear();
  int iteration = 0;
  decompress_result_t result;
  do {
    ++iteration;
    void* out = nullptr;
    size_t out_size = 0;
    result = decompress_next_chunk(&out, &out_size);
    ASSERT_NE(result, DECOMPRESS_FAILURE);

    output->insert(output->end(), reinterpret_cast<char*>(out),
                   reinterpret_cast<char*>(out) + out_size);
  } while (result == DECOMPRESS_CONTINUE);

  decompress_stop();

  if (loop_count) {
    *loop_count = iteration;
  }
}

// Decompress a small file to disk that can be processed in one shot.
TEST(Decompress, DecompressSmall) {
  std::string result;
  int loop_count = 0;
  ASSERT_NO_FATAL_FAILURE(Decompress(kFooLz4, sizeof(kFooLz4), &result, &loop_count));

  EXPECT_EQ(loop_count, 1);
  EXPECT_EQ(result, "foo");
}

// Decompress a large file to disk that will require multiple loops.
//
// The main requirement here is that the decompressed size must be >4MiB so
// that it cannot be decompressed into our 4MiB internal buffer in one shot.
TEST(Decompress, DecompressLarge) {
  std::string result;
  int loop_count = 0;
  ASSERT_NO_FATAL_FAILURE(
      Decompress(k6MiBLowercaseALz4, sizeof(k6MiBLowercaseALz4), &result, &loop_count));

  EXPECT_GT(loop_count, 1);
  EXPECT_EQ(result.size(), 6u * 1024 * 1024);
  for (char byte : result) {
    ASSERT_EQ(byte, 'a');
  }
}

// Ensure the state gets reset after each decompression.
//
// Start with a short file, then a longer file, then back to short to make sure
// no extra bytes are left hanging around.
//
// File creation:
//   $ echo -n abcdef >abcdef.raw
//   $ lz4 --rm abcdef.raw abcdef.lz4
TEST(Decompress, MultipleCalls) {
  std::string result;

  ASSERT_NO_FATAL_FAILURE(Decompress(kFooLz4, sizeof(kFooLz4), &result));
  EXPECT_EQ(result, "foo");

  ASSERT_NO_FATAL_FAILURE(Decompress(kAbcdefLz4, sizeof(kAbcdefLz4), &result));
  EXPECT_EQ(result, "abcdef");

  ASSERT_NO_FATAL_FAILURE(Decompress(kFooLz4, sizeof(kFooLz4), &result));
  EXPECT_EQ(result, "foo");
}

TEST(Decompress, InvalidImage) {
  std::vector<uint8_t> invalid{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};

  void* out = nullptr;
  size_t out_size = 0;

  ASSERT_TRUE(decompress_start(invalid.data(), invalid.size()));
  ASSERT_EQ(decompress_next_chunk(&out, &out_size), DECOMPRESS_FAILURE);
  decompress_stop();
}

TEST(Decompress, StartWhileRunningFails) {
  std::vector<uint8_t> data{0x00, 0x11};

  ASSERT_TRUE(decompress_start(data.data(), data.size()));
  ASSERT_FALSE(decompress_start(data.data(), data.size()));
  decompress_stop();
}

// Verify decompress_stop() is safe to call when no decompression is happening.
TEST(Decompress, DecompressStopNoOp) {
  decompress_stop();
  decompress_stop();
}

}  // namespace
