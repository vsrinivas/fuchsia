// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/firestore/encoding.h"

#include <string>

#include "gtest/gtest.h"

namespace cloud_provider_firestore {

namespace {

// Creates correct std::strings with \0 bytes inside from C-style string
// constants.
std::string operator"" _s(const char* str, size_t size) {
  return std::string(str, size);
}

class EncodingTest : public ::testing::TestWithParam<std::string> {};

TEST_P(EncodingTest, BackAndForth) {
  std::string data = GetParam();
  std::string encoded;
  std::string decoded;

  encoded = EncodeKey(data);
  EXPECT_EQ('+', encoded.back());
  EXPECT_TRUE(DecodeKey(encoded, &decoded));
  EXPECT_EQ(data, decoded);
}

INSTANTIATE_TEST_CASE_P(ExampleData,
                        EncodingTest,
                        ::testing::Values(""_s,
                                          "abcdef"_s,
                                          "\x02\x7F"_s,
                                          "~!@#$%^&*()_+-="_s,
                                          "\0"_s,
                                          "bazinga\0\0\0"_s));

}  // namespace

}  // namespace cloud_provider_firestore
