// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

TEST(FuzzerTest, EmptyInput) {
  EXPECT_EQ(0, LLVMFuzzerTestOneInput(nullptr, 0));
}

// TODO(aarongreen): Placeholder for now, until we figure out how we want to
// plumb the corpora from CIPD through to images built for test in CQ.
const char *kCorpusDir = "data/corpus";
TEST(FuzzerTest, WithCorpus) {
  if (!files::IsDirectory(kCorpusDir)) {
    return;
  }
  std::vector<std::string> inputs;
  std::vector<uint8_t> data;
  ASSERT_TRUE(files::ReadDirContents(kCorpusDir, &inputs));
  for (auto input : inputs) {
    ASSERT_TRUE(files::ReadFileToVector(input, &data));
    EXPECT_EQ(0, LLVMFuzzerTestOneInput(&data[0], data.size()));
  }
}
