// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dump-tests.h"
#include "test-tool-process.h"

// Much reader functionality is tested in dump-tests.cc in tandem with testing
// the corresponding parts of the dumper.  This file has more reader tests that
// only use the dumper incidentally and not to test it.

namespace {

using namespace std::literals;

TEST(ZxdumpTests, ReadZstdProcessDump) {
  // We'll verify the we can read the compressed dump stream by piping the raw
  // dump stream directly to the zstd tool to compress as a filter with pipes
  // on both ends, and then using the reader to read from that pipe.  (This
  // explicitly avoids using the ZstdWriter to have an independent test that
  // reading canonically compressed data works too.)
  zxdump::testing::TestToolProcess zstd;
  ASSERT_NO_FATAL_FAILURE(zstd.Init());
  std::vector<std::string> args({"-1"s, "-q"s});
  ASSERT_NO_FATAL_FAILURE(zstd.Start("zstd"s, args));
  ASSERT_NO_FATAL_FAILURE(zstd.CollectStderr());

  zxdump::testing::TestProcessForPropertiesAndInfo process;
  ASSERT_NO_FATAL_FAILURE(process.StartChild());
  {
    // Set up the writer to send the uncompressed data to the tool.
    zxdump::FdWriter writer(std::move(zstd.tool_stdin()));

    ASSERT_NO_FATAL_FAILURE(process.Dump(writer));

    // The write side of the pipe is closed when the writer goes out of scope,
    // so the decompressor can finish.
  }

  // Now read in the compressed dump stream and check its contents.
  zxdump::TaskHolder holder;
  auto read_result = holder.Insert(std::move(zstd.tool_stdout()), false);
  ASSERT_TRUE(read_result.is_ok()) << read_result.error_value();
  ASSERT_NO_FATAL_FAILURE(process.CheckDump(holder, false));

  // The reader should have consumed the all of the tool's stdout by now,
  // so it will have been unblocked to finish after its stdin hit EOF when
  // the writer's destruction closed the pipe.
  int exit_status;
  ASSERT_NO_FATAL_FAILURE(zstd.Finish(exit_status));
  EXPECT_EQ(exit_status, EXIT_SUCCESS);

  // The zstd tool shouldn't complain.
  EXPECT_EQ(zstd.collected_stderr(), "");
}

}  // namespace
