// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

namespace fuzzing {

static std::vector<std::string> gTestInputs;
static std::string gArgv0;

TEST(LlvmFuzzerTest, OneInput) {
  // Should work with null
  EXPECT_EQ(0, LLVMFuzzerTestOneInput(nullptr, 0));

  //  Should work with non-null but zero size
  uint8_t ignored;
  EXPECT_EQ(0, LLVMFuzzerTestOneInput(&ignored, 0));
}

TEST(LlvmFuzzerTest, SeedCorpus) {
#if defined(__Fuchsia__)
  FX_LOGS(INFO) << "Fuzzer built as test: " << gArgv0;
#endif

  // Should work with any files in directories specified on the command line.
  for (const auto &pathname : gTestInputs) {
    SCOPED_TRACE(pathname);
    std::vector<uint8_t> data;
    ASSERT_TRUE(files::ReadFileToVector(pathname, &data));
    EXPECT_EQ(0, LLVMFuzzerTestOneInput(&data[0], data.size()));
  }
}

TEST(LlvmFuzzerTest, MultipleCalls) {
  // Should work even when called multiple times consecutively
  EXPECT_EQ(0, LLVMFuzzerTestOneInput(nullptr, 0));
  EXPECT_EQ(0, LLVMFuzzerTestOneInput(nullptr, 0));
}

bool ExtractFlag(const std::string &flag, int *argc, char **argv) {
  int len = *argc;
  char **end = argv + len;
  char **src = std::find(argv, end, flag);
  if (src == end) {
    return false;
  }
  char **dst = src++;
  while (src != end) {
    *dst++ = *src++;
  }
  *argc = len - 1;
  return true;
}

}  // namespace fuzzing

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (::fuzzing::ExtractFlag("-h", &argc, argv)) {
    std::cout << std::endl << "usage: " << argv[0] << " [-q] <corpus-dir> [...]" << std::endl;
    return 0;
  }
  bool quiet = ::fuzzing::ExtractFlag("-q", &argc, argv);
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    std::vector<std::string> corpus;
    if (!files::ReadDirContents(arg, &corpus)) {
      if (!quiet) {
        FX_LOGS(WARNING) << "No such directory: " << arg << std::endl;
      }
      continue;
    }
    for (const auto &filename : corpus) {
      std::string pathname = arg + "/" + filename;
      if (files::IsFile(pathname)) {
        ::fuzzing::gTestInputs.push_back(pathname);
      }
    }
  }
  if (::fuzzing::gTestInputs.size() == 0 && !quiet) {
    FX_LOGS(WARNING) << "No inputs provided." << std::endl;
  } else {
    FX_LOGS(INFO) << "Testing with " << ::fuzzing::gTestInputs.size() << " inputs." << std::endl;
  }

  ::fuzzing::gArgv0 = argv[0];

  return RUN_ALL_TESTS();
}
