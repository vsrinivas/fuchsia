// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuzzer_test.h"

#include <stddef.h>
#include <stdint.h>

#include <gtest/gtest.h>

#include "src/lib/files/file.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

namespace fuzzing {

TEST(FuzzerTest, EmptyInput) { EXPECT_EQ(0, LLVMFuzzerTestOneInput(nullptr, 0)); }

TEST(FuzzerTest, WithCorpus) {
  auto elements = GetCorpus();
  std::vector<uint8_t> data;
  for (auto element : elements) {
    ASSERT_TRUE(files::ReadFileToVector(element, &data));
    EXPECT_EQ(0, LLVMFuzzerTestOneInput(&data[0], data.size()));
  }
}

}  // namespace fuzzing
