// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests for fdio_spawn's #! shebang directive support

#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/socket.h>
#include <string.h>
#include <unistd.h>
#include <zircon/errors.h>

#include <gtest/gtest.h>

#include "util.h"

namespace {

static constexpr char kUseScriptAsInterpreterBin[] = "/pkg/bin/use_script_as_interpreter";
static constexpr char kShebangEchoArgumentsBin[] = "/pkg/bin/shebang_echo_arguments";
static constexpr char kShebangInfiniteLoopBin[] = "/pkg/bin/shebang_infinite_loop";
static constexpr char kAttemptToUseShellOutsidePackageBin[] =
    "/pkg/bin/attempt_use_shell_outside_package.sh";
static constexpr char kTooLongShebangBin[] = "/pkg/bin/too_long_shebang";
static constexpr char kUseResolveFromShebangBin[] = "/pkg/bin/use_resolve_from_shebang";

class ShebangTest : public ::testing::Test {
 protected:
  void RunTest(const char* path, const char** argv, const char* expected) {
    int fd;
    zx::socket socket;
    zx_status_t status = fdio_pipe_half(&fd, socket.reset_and_get_address());
    ASSERT_EQ(status, ZX_OK);

    int flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO;
    fdio_spawn_action_t action = {.action = FDIO_SPAWN_ACTION_TRANSFER_FD,
                                  .fd = {.local_fd = fd, .target_fd = STDOUT_FILENO}};

    zx::process process;
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    status = fdio_spawn_etc(ZX_HANDLE_INVALID, flags, path, argv, NULL, 1, &action,
                            process.reset_and_get_address(), err_msg);
    if (status != ZX_OK) {
      fprintf(stderr, "fdio_spawn_etc failed: %s\n", err_msg);
    }
    ASSERT_EQ(status, ZX_OK);

    int64_t return_code;
    wait_for_process_exit(process, &return_code);
    EXPECT_EQ(return_code, 0);

    char buf[1024];
    size_t actual;
    status = socket.read(0, buf, sizeof(buf) - 1, &actual);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(actual, strlen(expected));
    buf[actual] = '\0';
    EXPECT_STREQ(buf, expected);
  }
};

// Should be able to spawn a shell script, assuming it uses a shell that is packaged
TEST_F(ShebangTest, SpawnShellScriptPath) {
  const char* path = kShebangEchoArgumentsBin;
  const char* argv[] = {path, "original_arg1", "original_arg2", nullptr};
  const char* expected =
      "/pkg/bin/echo_arguments_bin\n/pkg/bin/shebang_echo_arguments\n"
      "original_arg1\noriginal_arg2\n";
  RunTest(path, argv, expected);
}

// Multiple #! directives should be resolved
TEST_F(ShebangTest, SpawnScriptThatUsesOtherScript) {
  const char* path = kUseScriptAsInterpreterBin;
  const char* argv[] = {path, "original_arg1", "original_arg2", nullptr};

  // Note that the interpreter argument in use_script_as_interpreter becomes a single argument
  // containing a space.
  const char* expected =
      "/pkg/bin/echo_arguments_bin\n/pkg/bin/shebang_echo_arguments\n"
      "extra_arg1 extra_arg2\n/pkg/bin/use_script_as_interpreter\n"
      "original_arg1\noriginal_arg2\n";
  RunTest(path, argv, expected);
}

// Infinite #! loop (the file references itself) should fail after hitting the limit
TEST_F(ShebangTest, SpawnShebangInfiniteLoopFails) {
  const char* path = kShebangInfiniteLoopBin;
  const char* argv[] = {path, nullptr};

  zx::process process;
  zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, path, argv,
                                  process.reset_and_get_address());
  EXPECT_EQ(status, ZX_ERR_IO_INVALID);
  EXPECT_FALSE(process.is_valid());
}

// Trying to use an interpreter (say, a shell) that's outside the namespace should fail.
TEST_F(ShebangTest, SpawnShebangRespectsNamesapce) {
  const char* path = kAttemptToUseShellOutsidePackageBin;
  const char* argv[] = {path, nullptr};

  zx::process process;
  zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, path, argv,
                                  process.reset_and_get_address());
  EXPECT_EQ(status, ZX_ERR_INTERNAL);
  EXPECT_FALSE(process.is_valid());
}

// If the shebang directive is longer than the limit (FDIO_SPAWN_MAX_INTERPRETER_LINE_LEN), spawn
// should fail.
TEST_F(ShebangTest, SpawnFailsIfShebangIsTooLong) {
  const char* path = kTooLongShebangBin;
  const char* argv[] = {path, nullptr};

  zx::process process;
  zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, path, argv,
                                  process.reset_and_get_address());
  EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE);
  EXPECT_FALSE(process.is_valid());
}

// Using a shebang to load a file that uses #!resolve should fail; mixing the two is unsupported.
TEST_F(ShebangTest, SpawnFailsIfShebangUsesResolve) {
  const char* path = kUseResolveFromShebangBin;
  const char* argv[] = {path, nullptr};

  zx::process process;
  zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, path, argv,
                                  process.reset_and_get_address());
  EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
  EXPECT_FALSE(process.is_valid());
}

}  // namespace
