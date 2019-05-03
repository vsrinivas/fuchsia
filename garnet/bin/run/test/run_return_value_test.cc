// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <src/lib/files/file.h>
#include <stdlib.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "gtest/gtest.h"

static constexpr char kRunPath[] = "/bin/run";
static constexpr char kExiter[] =
    "fuchsia-pkg://fuchsia.com/run_test_exiter#meta/run_test_exiter.cmx";
static constexpr char kExiterShort[] = "run_test_exiter.cmx";
static constexpr char kStdout[] =
    "Found fuchsia-pkg://fuchsia.com/run_test_exiter#meta/run_test_exiter.cmx, "
    "executing.\n";

void test_case(const char* url, const char* value) {
  std::FILE* outf = std::tmpfile();
  int out_fd = fileno(outf);
  fdio_spawn_action_t actions[] = {
      {.action = FDIO_SPAWN_ACTION_CLONE_FD,
       .fd = {.local_fd = STDIN_FILENO, .target_fd = STDIN_FILENO}},
      {.action = FDIO_SPAWN_ACTION_CLONE_FD,
       .fd = {.local_fd = dup(out_fd), .target_fd = STDOUT_FILENO}},
      {.action = FDIO_SPAWN_ACTION_CLONE_FD,
       .fd = {.local_fd = STDERR_FILENO, .target_fd = STDERR_FILENO}},
  };

  // Spawn "run run_test_exiter <value>"
  uint32_t flags = FDIO_SPAWN_CLONE_ALL;
  const char* argv[] = {kRunPath, url, value, NULL};
  zx_handle_t process = ZX_HANDLE_INVALID;
  zx_status_t status = fdio_spawn_etc(ZX_HANDLE_INVALID, flags, kRunPath, argv,
                                      NULL, 2, actions, &process, nullptr);
  ASSERT_EQ(ZX_OK, status);

  // Wait for `run` to terminate
  status =
      zx_object_wait_one(process, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, NULL);
  ASSERT_EQ(ZX_OK, status);

  std::string output;
  ASSERT_TRUE(files::ReadFileDescriptorToString(out_fd, &output));
  if (url == kExiterShort) {
    ASSERT_EQ(kStdout, output);
  }

  // Verify `run` return code
  zx_info_process_t proc_info;
  status = zx_object_get_info(process, ZX_INFO_PROCESS, &proc_info,
                              sizeof(proc_info), NULL, NULL);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(strtoll(value, NULL, 0), proc_info.return_code);
}

TEST(RunReturnValueTest, Zero) { test_case(kExiter, "0"); }
TEST(RunReturnValueTest, OneTwoThree) { test_case(kExiter, "123"); }
TEST(RunReturnValueTest, Negative) { test_case(kExiter, "-99999"); }
TEST(RunReturnValueTest, LongValue) {
  test_case(kExiter, "1152921504606846976");
}
TEST(RunReturnValueTest, FuzzySearchZero) { test_case(kExiterShort, "0"); }
TEST(RunReturnValueTest, FuzzySearchOneTwoThree) {
  test_case(kExiterShort, "123");
}
TEST(RunReturnValueTest, FuzzySearchNegative) {
  test_case(kExiterShort, "-99999");
}
TEST(RunReturnValueTest, FuzzySearchLongValue) {
  test_case(kExiterShort, "1152921504606846976");
}
