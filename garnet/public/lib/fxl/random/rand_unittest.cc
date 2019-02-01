// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/random/rand.h"

#include <stdint.h>

#include <iterator>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace fxl {
namespace {

class RandBytesTest : public ::testing::TestWithParam<size_t> {};

TEST_P(RandBytesTest, GenerateBytes) {
  const size_t array_size = GetParam();  // Test parameter

  // The array version of std::make_unique value-initializes so this will be
  // filled with zeros, but just to make it clear we assert it too.
  auto bytes = std::make_unique<char[]>(array_size);
  const char* const bytes_begin = bytes.get();
  const char* const bytes_end = bytes.get() + array_size;
  auto is_nonzero = [](char v) { return v != 0; };
  ASSERT_EQ(std::count_if(bytes_begin, bytes_end, is_nonzero), 0);

  RandBytes(bytes.get(), array_size);

  // Check that all the bytes do not match. This will catch both the case where
  // the array was not filled at all (still all zeros) as well as a generator
  // that simply produces a constant non-zero value.
  auto ne_first_byte = [&](char v) { return v != bytes[0]; };
  ASSERT_GT(std::count_if(bytes_begin, bytes_end, ne_first_byte), 0);
}

// Minimum byte size here is chosen to minimize the probability of a false
// positive flake (i.e. actually randomly generating an array of all the same
// byte). If the generator was uniformly distributed, an 8 byte array yields a
// probability of a flake only 1 in every 1/(2^8*(1/(2^8))^8) or 2^56 runs.
INSTANTIATE_TEST_CASE_P(DifferentSizes, RandBytesTest,
                        ::testing::Values(8, 16, 17, 512, 2048, 2049));

TEST(Random, RandUint64) {
  for (int i = 0; i < 256; ++i) {
    uint64_t num = RandUint64();
    (void)num;
  }
}

}  // namespace
}  // namespace fxl
