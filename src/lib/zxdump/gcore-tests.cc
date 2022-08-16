// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/layout.h>
#include <lib/stdcompat/string_view.h>

#include <cstdlib>

#include <gtest/gtest.h>

#include "dump-tests.h"
#include "test-tool-process.h"

namespace {

constexpr const char* kOutputSwitch = "-o";
constexpr const char* kExcludeMemorySwitch = "--exclude-memory";

void UsageTest(int expected_status, const std::vector<std::string>& args = {}) {
  zxdump::testing::TestToolProcess child;
  ASSERT_NO_FATAL_FAILURE(child.Start("gcore", args));
  ASSERT_NO_FATAL_FAILURE(child.CollectStdout());
  ASSERT_NO_FATAL_FAILURE(child.CollectStderr());
  int status;
  ASSERT_NO_FATAL_FAILURE(child.Finish(status));
  EXPECT_EQ(status, expected_status);
  EXPECT_EQ(child.collected_stdout(), "");
  std::string text = child.collected_stderr();
  EXPECT_TRUE(cpp20::starts_with(std::string_view(text), "Usage: "));
  EXPECT_TRUE(cpp20::ends_with(std::string_view(text), '\n'));
}

TEST(ZxdumpTests, GcoreHelp) { UsageTest(EXIT_SUCCESS, {"--help"}); }

TEST(ZxdumpTests, GcoreUsage) { UsageTest(EXIT_FAILURE); }

TEST(Zxdump, GcoreProcessDumpIsElfCore) {
  zxdump::testing::TestProcess process;
  ASSERT_NO_FATAL_FAILURE(process.StartChild());
  const std::string pid_string = std::to_string(process.koid());

  zxdump::testing::TestToolProcess child;
  auto& dump_file = child.MakeFile("process-dump", "." + pid_string);
  std::string prefix = dump_file.name();
  prefix.resize(prefix.size() - pid_string.size());
  std::vector<std::string> args({
      // Don't dump memory since we don't need it and it is large.
      kExcludeMemorySwitch,
      kOutputSwitch,
      prefix,
      pid_string,
  });
  ASSERT_NO_FATAL_FAILURE(child.Start("gcore", args));
  ASSERT_NO_FATAL_FAILURE(child.CollectStdout());
  ASSERT_NO_FATAL_FAILURE(child.CollectStderr());
  int status;
  ASSERT_NO_FATAL_FAILURE(child.Finish(status));
  EXPECT_EQ(status, EXIT_SUCCESS);
  EXPECT_EQ(child.collected_stdout(), "");
  EXPECT_EQ(child.collected_stderr(), "");

  fbl::unique_fd fd = dump_file.OpenOutput();
  ASSERT_TRUE(fd);
  elfldltl::Elf<>::Ehdr ehdr;
  ASSERT_EQ(read(fd.get(), &ehdr, sizeof(ehdr)), static_cast<ssize_t>(sizeof(ehdr)))
      << strerror(errno);
  EXPECT_TRUE(ehdr.Valid());
  EXPECT_EQ(ehdr.type, elfldltl::ElfType::kCore);
}

}  // namespace
