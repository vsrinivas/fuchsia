// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <iterator>
#include <vector>

#include <fbl/algorithm.h>
#include <zxtest/zxtest.h>

// This is copy/pasted from fxl, which we can't depend on here.
//
// TODO(dustingreen): Remove this impl after we can depend on
// HANDLE_EINTR and ReadFileDescriptorToString from here.
#define HANDLE_EINTR(x)                                                                      \
  ({                                                                                         \
    int eintr_wrapper_counter = 0;                                                           \
    decltype(x) eintr_wrapper_result;                                                        \
    do {                                                                                     \
      eintr_wrapper_result = (x);                                                            \
    } while (eintr_wrapper_result == -1 && errno == EINTR && eintr_wrapper_counter++ < 100); \
    eintr_wrapper_result;                                                                    \
  })
namespace files {
template <typename T>
bool ReadFileDescriptor(int fd, T* result) {
  assert(result != nullptr);
  result->clear();

  if (fd < 0)
    return false;

  constexpr size_t kBufferSize = 1 << 16;
  size_t offset = 0;
  ssize_t bytes_read = 0;
  do {
    offset += bytes_read;
    result->resize(offset + kBufferSize);
    bytes_read = HANDLE_EINTR(read(fd, &(*result)[offset], kBufferSize));
  } while (bytes_read > 0);

  if (bytes_read < 0) {
    result->clear();
    return false;
  }

  result->resize(offset + bytes_read);
  return true;
}
bool ReadFileDescriptorToString(int fd, std::string* result) {
  return ReadFileDescriptor(fd, result);
}
}  // namespace files

namespace {

constexpr char kExpectedPanicMessage[] = "This message should be seen on stderr.  42\n";
constexpr char kUnexpectedPanicMessage[] = "This message should not be seen on stderr.\n";

const char* process_bin;

// This runs in a separate process, since the expected outcome of running this
// function is that the process aborts().  It is launched by the PanicAProcess
// test.
void panic_this_process() {
  // Not using ZX_ASSERT() for this, since ZX_PANIC() is what we're testing.
  if (stderr == nullptr) {
    abort();
  }
  ZX_PANIC("This message should be seen on stderr.  %d", 42);
  ZX_PANIC("This message should not be seen on stderr.");
}

TEST(ZirconUserPanicTestCase, StderrOutputAndProcessTerminates) {
  int pipefd[2];
  ASSERT_EQ(0, pipe(pipefd));
  const std::vector<const char*> args = {process_bin, "child", nullptr};
  fdio_spawn_action_t actions[] = {{.action = FDIO_SPAWN_ACTION_CLONE_FD,
                                    .fd = {.local_fd = STDOUT_FILENO, .target_fd = STDOUT_FILENO}},
                                   {.action = FDIO_SPAWN_ACTION_CLONE_FD,
                                    .fd = {.local_fd = STDIN_FILENO, .target_fd = STDIN_FILENO}},
                                   {.action = FDIO_SPAWN_ACTION_CLONE_FD,
                                    .fd = {.local_fd = pipefd[1], .target_fd = STDERR_FILENO}}};
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx::process proc;
  zx_status_t status = fdio_spawn_etc(
      ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO, process_bin, args.data(),
      nullptr, std::size(actions), actions, proc.reset_and_get_address(), err_msg);
  ASSERT_OK(status);

  // Close our handle to the other end of the pipe so it closes, so we don't
  // wait forever.
  close(pipefd[1]);

  std::string stderr_output;
  ASSERT_TRUE(files::ReadFileDescriptorToString(pipefd[0], &stderr_output));

  EXPECT_NE(stderr_output.find(kExpectedPanicMessage), std::string::npos);
  EXPECT_EQ(stderr_output.find(kUnexpectedPanicMessage), std::string::npos);

  ASSERT_OK(proc.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr));
}

}  // namespace

int main(int argc, char** argv) {
  process_bin = argv[0];
  if (argc > 1 && !strcmp(argv[1], "child")) {
    panic_this_process();
    // Avoid exiting if panic_this_process() somehow returns.
    for (;;)
      ;
    // Not reachable.
    return -1;
  }
  return RUN_ALL_TESTS(argc, argv);
}
