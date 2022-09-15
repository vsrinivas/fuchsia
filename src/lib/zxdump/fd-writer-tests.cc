// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <lib/zxdump/fd-writer.h>
#include <stdio.h>
#include <unistd.h>

#include <string_view>

#include <fbl/unique_fd.h>

#include "test-pipe-reader.h"
#include "writer-tests.h"

namespace {

TEST(ZxdumpTests, FdWriterToFile) {
  FILE* tmpf = tmpfile();
  ASSERT_TRUE(tmpf);
  auto close_tmpf = fit::defer([tmpf]() { fclose(tmpf); });

  fbl::unique_fd tmpfd(dup(fileno(tmpf)));
  ASSERT_TRUE(tmpfd);

  zxdump::FdWriter writer(std::move(tmpfd));

  // Pump some stock test data through the writer API.
  zxdump::testing::WriterTest::WriteTestData(writer);

  // Now verify the data written to the file.
  rewind(tmpf);
  EXPECT_FALSE(ferror(tmpf));
  char buf[zxdump::testing::WriterTest::kTestData.size()];
  size_t n = fread(buf, 1, sizeof(buf), tmpf);
  EXPECT_FALSE(ferror(tmpf));
  ASSERT_LE(n, sizeof(buf));
  EXPECT_EQ(n, zxdump::testing::WriterTest::kTestData.size());
  std::string_view tmpf_contents{buf, n};
  EXPECT_EQ(tmpf_contents, zxdump::testing::WriterTest::kTestData);
}

TEST(ZxdumpTests, FdWriterToPipe) {
  zxdump::testing::TestPipeReader reader;
  fbl::unique_fd write_pipe;
  ASSERT_NO_FATAL_FAILURE(reader.Init(write_pipe));

  {
    zxdump::FdWriter writer(std::move(write_pipe));

    // Pump some stock test data through the writer API.
    zxdump::testing::WriterTest::WriteTestData(writer);

    // The write side of the pipe is closed when the writer goes out of scope,
    // so the reader can finish.
  }

  std::string contents = std::move(reader).Finish();
  EXPECT_EQ(contents, zxdump::testing::WriterTest::kTestData);
}

}  // namespace
