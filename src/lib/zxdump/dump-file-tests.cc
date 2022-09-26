// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxdump/types.h>

#include <gtest/gtest.h>

#include "dump-file.h"
#include "test-file.h"
#include "test-tool-process.h"

namespace {

using namespace std::literals;

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

TEST(ZxdumpTests, DumpFileZstd) {
  constexpr std::string_view kTestData = "test data";

  // Write a compressed file with the zstd tool.
  zxdump::testing::TestToolProcess zstd;
  ASSERT_NO_FATAL_FAILURE(zstd.Init());
  zxdump::testing::TestToolProcess::File& zstd_file =
      zstd.MakeFile("dump-file-zstd", zxdump::testing::TestToolProcess::File::kZstdSuffix);
  std::vector<std::string> args({"-q"s, "-o"s, zstd_file.name()});
  ASSERT_NO_FATAL_FAILURE(zstd.Start("zstd"s, args));
  ASSERT_NO_FATAL_FAILURE(zstd.SendStdin(std::string(kTestData)));
  int exit_status;
  ASSERT_NO_FATAL_FAILURE(zstd.Finish(exit_status));
  EXPECT_EQ(exit_status, EXIT_SUCCESS);

  auto file = zxdump::internal::DumpFile::Open(zstd_file.OpenOutput());
  ASSERT_TRUE(file.is_ok()) << zstd.FilePathForRunner(zstd_file) << ": " << file.error_value();

  auto header = file->ReadEphemeral({0, zxdump::internal::kHeaderProbeSize});
  ASSERT_TRUE(header.is_ok()) << header.error_value();

  ASSERT_FALSE(header->empty());
  ASSERT_TRUE(zxdump::internal::DumpFile::IsCompressed(*header));

  auto decompressed = file->Decompress({0, file->size()}, *header);
  ASSERT_TRUE(decompressed.is_ok()) << decompressed.error_value();

  // The reported size is not really meaningful, since it's streaming input.
  // But it's guaranteed to be nonzero.
  EXPECT_GT(decompressed->size(), 0u);
  EXPECT_GT(decompressed->size_bytes(), 0u);

  auto bytes = decompressed->ReadEphemeral({0, kTestData.size()});
  ASSERT_TRUE(bytes.is_ok()) << bytes.error_value();
  std::string_view str{
      reinterpret_cast<const char*>(bytes->data()),
      bytes->size(),
  };
  EXPECT_EQ(str, kTestData);
}

}  // namespace
