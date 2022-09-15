// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <lib/zxdump/zstd-writer.h>
#include <stdio.h>

#include <string_view>

#include <fbl/unique_fd.h>

#include "test-pipe-reader.h"
#include "test-tool-process.h"
#include "writer-tests.h"

namespace {

using namespace std::literals;

TEST(ZxdumpTests, ZstdWriterToFile) {
  // We'll verify the data written to the file by decompressing it with the
  // zstd tool and catching the output via pipe.
  zxdump::testing::TestToolProcess zstd;
  ASSERT_NO_FATAL_FAILURE(zstd.Init());

  // Set up the writer to send the compressed data to a temporary file.
  zxdump::testing::TestToolProcess::File& zstd_file =
      zstd.MakeFile("zstd-writer-test", zxdump::testing::TestToolProcess::File::kZstdSuffix);
  zxdump::ZstdWriter writer(zstd_file.CreateInput());

  // Pump some stock test data through the writer API.
  zxdump::testing::WriterTest::WriteTestData(writer);

  // Complete the compressed stream.
  auto finish = writer.Finish();
  ASSERT_TRUE(finish.is_ok()) << finish.error_value();

  // Now decompress the file onto the collected stdout.
  std::vector<std::string> args({"-dc"s, zstd_file.name()});
  ASSERT_NO_FATAL_FAILURE(zstd.Start("zstd"s, args));
  ASSERT_NO_FATAL_FAILURE(zstd.CollectStdout());
  ASSERT_NO_FATAL_FAILURE(zstd.CollectStderr());
  int exit_status;
  ASSERT_NO_FATAL_FAILURE(zstd.Finish(exit_status));
  EXPECT_EQ(exit_status, EXIT_SUCCESS);

  // The zstd tool would complain about a malformed file.
  EXPECT_EQ(zstd.collected_stderr(), "");

  // It wrote out the decompressed data, which should match what went in.
  EXPECT_EQ(zstd.collected_stdout(), zxdump::testing::WriterTest::kTestData);
}

TEST(ZxdumpTests, ZstdWriterToPipe) {
  // As above, but using the zstd tool as a filter with pipes on both ends.
  zxdump::testing::TestToolProcess zstd;
  ASSERT_NO_FATAL_FAILURE(zstd.Init());

  // Use the write side of the reader's pipe as the tool's stdout.
  zxdump::testing::TestPipeReader reader;
  ASSERT_NO_FATAL_FAILURE(reader.Init(zstd.tool_stdout()));

  // Now start the decompressor running as a filter.
  std::vector<std::string> args({"-dc"s});
  ASSERT_NO_FATAL_FAILURE(zstd.Start("zstd"s, args));
  ASSERT_NO_FATAL_FAILURE(zstd.CollectStderr());

  {
    // Set up the writer to send the compressed data to the tool's stdin.
    zxdump::ZstdWriter writer(std::move(zstd.tool_stdin()));

    // Pump some stock test data through the writer API.
    zxdump::testing::WriterTest::WriteTestData(writer);

    // Complete the compressed stream.
    auto finish = writer.Finish();
    ASSERT_TRUE(finish.is_ok()) << finish.error_value();

    // The write side of the pipe is closed when the writer goes out of scope,
    // so the reader can finish.
  }

  // Let the decompressor finish.
  int exit_status;
  ASSERT_NO_FATAL_FAILURE(zstd.Finish(exit_status));
  EXPECT_EQ(exit_status, EXIT_SUCCESS);

  // The zstd tool would complain about a malformed stream.
  EXPECT_EQ(zstd.collected_stderr(), "");

  // It wrote out the decompressed data, which should match what went in.
  std::string contents = std::move(reader).Finish();
  EXPECT_EQ(contents, zxdump::testing::WriterTest::kTestData);
}

}  // namespace
