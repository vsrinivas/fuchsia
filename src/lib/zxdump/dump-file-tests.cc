// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxdump/types.h>

#include <gtest/gtest.h>

#include "dump-file.h"
#include "test-file.h"

namespace {

TEST(ZxdumpTests, DumpFileBadOpen) {
  auto result = zxdump::internal::DumpFile::Open({});
  EXPECT_TRUE(result.is_error());
}

TEST(ZxdumpTests, DumpFileMmap) {
  const std::string test_data = "test data";

  zxdump::testing::TestFile test_file;
  fputs(test_data.c_str(), test_file.stdio());

  auto file = zxdump::internal::DumpFile::Open(test_file.RewoundFd(), true);
  ASSERT_TRUE(file.is_ok()) << file.error_value();

  EXPECT_EQ(file->size(), test_data.size());
  EXPECT_EQ(file->size_bytes(), test_data.size());

  auto bytes = file->ReadEphemeral({0, test_data.size()});
  ASSERT_TRUE(bytes.is_ok()) << bytes.error_value();
  std::string_view str{
      reinterpret_cast<const char*>(bytes->data()),
      bytes->size(),
  };
  EXPECT_EQ(str, test_data);
}

TEST(ZxdumpTests, DumpFileStdio) {
  const std::string test_data = "test data";

  zxdump::testing::TestFile test_file;
  fputs(test_data.c_str(), test_file.stdio());

  auto file = zxdump::internal::DumpFile::Open(test_file.RewoundFd(), false);
  ASSERT_TRUE(file.is_ok()) << file.error_value();

  EXPECT_EQ(file->size(), test_data.size());
  EXPECT_EQ(file->size_bytes(), test_data.size());

  auto bytes = file->ReadEphemeral({0, test_data.size()});
  ASSERT_TRUE(bytes.is_ok()) << bytes.error_value();
  std::string_view str{
      reinterpret_cast<const char*>(bytes->data()),
      bytes->size(),
  };
  EXPECT_EQ(str, test_data);
}

TEST(ZxdumpTests, DumpFilePipe) {
  const std::string test_data = "test data";

  int pipefd[2];
  ASSERT_EQ(pipe(pipefd), 0) << strerror(errno);
  fbl::unique_fd in(pipefd[0]);

  {
    FILE* outf = fdopen(pipefd[1], "w");
    ASSERT_TRUE(outf) << strerror(errno);
    fputs(test_data.c_str(), outf);
    fclose(outf);
  }

  auto file = zxdump::internal::DumpFile::Open(std::move(in));
  ASSERT_TRUE(file.is_ok()) << file.error_value();

  EXPECT_EQ(file->size(), std::numeric_limits<size_t>::max());
  EXPECT_EQ(file->size_bytes(), std::numeric_limits<size_t>::max());

  // Even though it's not seekable, we can re-read the last-read chunk.
  for (int i = 0; i < 100; ++i) {
    auto bytes = file->ReadEphemeral({0, test_data.size()});
    ASSERT_TRUE(bytes.is_ok()) << bytes.error_value();
    std::string_view str{
        reinterpret_cast<const char*>(bytes->data()),
        bytes->size(),
    };
    EXPECT_EQ(str, test_data);
  }
}

}  // namespace
