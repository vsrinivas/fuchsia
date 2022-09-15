// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fit/defer.h>

#include <cerrno>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "piped-command.h"
#include "test-tool-process.h"

#ifndef __Fuchsia__
#include <sys/wait.h>
#endif

namespace {

constexpr const char* kTestChild = "zxdump-test-child";

// TestToolProcess uses ToolPath implicitly, but PipedCommand does not.
std::string TestChildPath() { return zxdump::testing::ToolPath(kTestChild); }

#ifdef __Fuchsia__

bool NoChildren() { return true; }

bool IsProcess(const zx::process& process) { return process.is_valid(); }

void WaitFor(zx::process process, int& status) {
  ASSERT_TRUE(process);
  zx_signals_t signals = 0;
  ASSERT_EQ(process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), &signals), ZX_OK);
  ASSERT_TRUE(signals & ZX_PROCESS_TERMINATED);
  zx_info_process_t info;
  ASSERT_EQ(process.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr), ZX_OK);
  status = static_cast<int>(info.return_code);
}

#else

bool NoChildren() { return waitpid(-1, nullptr, WNOHANG) < 0 && errno == ECHILD; }

bool IsProcess(int pid) { return pid > 0; }

void WaitFor(int pid, int& status) {
  ASSERT_GT(pid, 0);
  ASSERT_EQ(waitpid(pid, &status, 0), pid) << "waitpid: " << pid << ": " << strerror(errno);
  status = WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
}

#endif

TEST(ZxdumpTests, PipedCommandStart) {
  ASSERT_TRUE(NoChildren());
  {
    zxdump::PipedCommand child;
    auto result = child.Start(TestChildPath(), {"-x", "0"});
    ASSERT_TRUE(result.is_ok()) << result.error_value();
  }
  // The destructor waited for the child to die.
  ASSERT_TRUE(NoChildren());
}

TEST(ZxdumpTests, PipedCommandProcessOwnership) {
  decltype(zxdump::PipedCommand().process()) process;
  {
    zxdump::PipedCommand child;
    auto result = child.Start(TestChildPath(), {"-x", "42"});
    ASSERT_TRUE(result.is_ok()) << result.error_value();
    ASSERT_TRUE(IsProcess(child.process()));
    process = std::move(child).process();
    ASSERT_FALSE(IsProcess(child.process()));
  }
  ASSERT_TRUE(IsProcess(process));
  int status;
  ASSERT_NO_FATAL_FAILURE(WaitFor(std::move(process), status));
  EXPECT_EQ(status, 42);
}

TEST(ZxdumpTests, PipedCommandRedirect) {
  // TestToolProcess uses PipedCommand::Redirect under the hood and
  // takes care of pushing to its stdin and pulling from its stdout.
  constexpr std::string_view kStdinContents = "stdin contents";
  zxdump::testing::TestToolProcess child;
  ASSERT_NO_FATAL_FAILURE(child.Init());
  ASSERT_NO_FATAL_FAILURE(child.Start(kTestChild, {"-c", "-", "-x", "0"}));
  ASSERT_NO_FATAL_FAILURE(child.CollectStdout());
  ASSERT_NO_FATAL_FAILURE(child.CollectStderr());
  ASSERT_NO_FATAL_FAILURE(child.SendStdin(std::string{kStdinContents}));
  int status;
  ASSERT_NO_FATAL_FAILURE(child.Finish(status));
  EXPECT_EQ(status, 0);
  EXPECT_EQ(child.collected_stdout(), kStdinContents);
  EXPECT_EQ(child.collected_stderr(), "");
}

TEST(ZxdumpTests, TestToolProcessInputFile) {
  constexpr std::string_view kFileContents = "file contents";
  std::string in_file_name;
  {
    zxdump::testing::TestToolProcess child;
    ASSERT_NO_FATAL_FAILURE(child.Init());
    auto& in_file = child.MakeFile("input");
    in_file_name = in_file.name();
    EXPECT_NE(in_file_name.find("input"), std::string::npos);
    {
      fbl::unique_fd in_fd = in_file.CreateInput();
      ASSERT_TRUE(in_fd) << strerror(errno);
      FILE* f = fdopen(in_fd.release(), "w");
      auto cleanup_f = fit::defer([f]() {
        if (f) {
          fclose(f);
        }
      });
      ASSERT_TRUE(f) << strerror(errno);
      fwrite(kFileContents.data(), kFileContents.size(), 1, f);
      ASSERT_FALSE(ferror(f));
    }
    ASSERT_NO_FATAL_FAILURE(child.Start(kTestChild, {"-c", in_file_name, "-x", "0"}));
    ASSERT_NO_FATAL_FAILURE(child.CollectStdout());
    ASSERT_NO_FATAL_FAILURE(child.CollectStderr());
    int status;
    ASSERT_NO_FATAL_FAILURE(child.Finish(status));
    EXPECT_EQ(status, 0);
    EXPECT_EQ(child.collected_stdout(), kFileContents);
    EXPECT_EQ(child.collected_stderr(), "");
  }
  // Should have been cleaned up on destruction.
  EXPECT_FALSE(fbl::unique_fd{open(in_file_name.c_str(), O_RDONLY)});
  EXPECT_EQ(errno, ENOENT);
}

TEST(ZxdumpTests, TestToolProcessOutputFile) {
  constexpr std::string_view kFileContents = "file contents";
  zxdump::testing::TestToolProcess child;
  ASSERT_NO_FATAL_FAILURE(child.Init());
  auto& out_file = child.MakeFile("output");
  const std::string out_file_name = out_file.name();
  EXPECT_NE(out_file_name.find("output"), std::string::npos);
  ASSERT_NO_FATAL_FAILURE(child.Start(kTestChild, {"-o", out_file_name, "-x", "0"}));
  ASSERT_NO_FATAL_FAILURE(child.CollectStdout());
  ASSERT_NO_FATAL_FAILURE(child.CollectStderr());
  ASSERT_NO_FATAL_FAILURE(child.SendStdin(std::string{kFileContents}));
  int status;
  ASSERT_NO_FATAL_FAILURE(child.Finish(status));
  EXPECT_EQ(status, 0);
  EXPECT_EQ(child.collected_stdout(), "");
  EXPECT_EQ(child.collected_stderr(), "");
  EXPECT_EQ(out_file.OutputContents(), kFileContents);
}

}  // namespace
