// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <thread>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <runtests-utils/posix-run-test.h>
#include <unittest/unittest.h>

namespace runtests {
namespace {

// A list of the names of environment variables names that we pass into the
// spawned test subprocess.
constexpr const char* const kAllowedEnvironmentVars[] = {
    "TMPDIR",
    "PATH",
    // Paths to the symbolizer for various sanitizers.
    "ASAN_SYMBOLIZER_PATH",
    "LSAN_SYMBOLIZER_PATH",
    "MSAN_SYMBOLIZER_PATH",
    "UBSAN_SYMBOLIZER_PATH",
    // From unittest.h. Set by RunAllTests().
    TEST_ENV_NAME,
};

// How long to sleep between checking to see if a test is finished.
// We do have tests that take >10 ms to run, so it's good for this
// to be on the smaller side.
constexpr std::chrono::milliseconds kPollingInterval(2);

}  // namespace

int64_t msec_since(std::chrono::steady_clock::time_point start_time) {
  const auto end_time = std::chrono::steady_clock::now();
  const auto duration = end_time - start_time;
  return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

std::unique_ptr<Result> PosixRunTest(const char* argv[],
                                     const char*,  // output_dir
                                     const char* output_filename, const char* test_name,
                                     uint64_t timeout_msec) {
  int status;
  const char* path = argv[0];
  FILE* output_file = nullptr;

  // Initialize |file_actions|, which dictate what I/O will be performed in the
  // launched process, and ensure its destruction on function exit.
  posix_spawn_file_actions_t file_actions;
  if ((status = posix_spawn_file_actions_init(&file_actions))) {
    fprintf(stderr, "FAILURE: posix_spawn_file_actions_init failed: %s\n", strerror(status));
    return std::make_unique<Result>(test_name, FAILED_TO_LAUNCH, 0, 0);
  }

  auto auto_tidy = fbl::MakeAutoCall([&] {
    posix_spawn_file_actions_destroy(&file_actions);
    if (output_file != nullptr) {
      fclose(output_file);
    }
  });

  // Construct the array of allowed environment variable strings of the
  // form "<name>=<value>".  The env_strings array just keeps the underlying
  // std::string objects alive so the envp pointers remain valid.
  std::string env_strings[fbl::count_of(kAllowedEnvironmentVars)];
  const char* envp[fbl::count_of(env_strings) + 1];  // +1 for null terminator.
  size_t i = 0;
  for (const char* var : kAllowedEnvironmentVars) {
    const char* val = getenv(var);
    if (val) {
      env_strings[i] = std::string(var) + "=" + val;
      envp[i] = env_strings[i].c_str();
      ++i;
    }
  }
  envp[i] = nullptr;

  // Tee output.
  if (output_filename != nullptr) {
    output_file = fopen(output_filename, "w");
    if (output_file == nullptr) {
      return std::make_unique<Result>(test_name, FAILED_DURING_IO, 0, 0);
    }
    if ((status = posix_spawn_file_actions_addopen(&file_actions, STDOUT_FILENO, output_filename,
                                                   O_WRONLY | O_CREAT | O_TRUNC, 0644))) {
      fprintf(stderr, "FAILURE: posix_spawn_file_actions_addopen failed: %s\n", strerror(status));
      return std::make_unique<Result>(test_name, FAILED_TO_LAUNCH, 0, 0);
    }
    if ((status = posix_spawn_file_actions_adddup2(&file_actions, STDOUT_FILENO, STDERR_FILENO))) {
      fprintf(stderr, "FAILURE: posix_spawn_file_actions_adddup2 failed: %s\n", strerror(status));
      return std::make_unique<Result>(test_name, FAILED_TO_LAUNCH, 0, 0);
    }
  }

  // Launch the test subprocess.
  pid_t test_pid;

  const auto start_time = std::chrono::steady_clock::now();
  if ((status = posix_spawn(&test_pid, path, &file_actions, nullptr, const_cast<char**>(argv),
                            const_cast<char**>(envp)))) {
    fprintf(stderr, "FAILURE: posix_spawn failed: %s\n", strerror(status));
    return std::make_unique<Result>(test_name, FAILED_TO_LAUNCH, 0, 0);
  }

  pid_t wait_ret = 0;
  // WNOHANG means return 0 immediately if the process hasn't exited.
  while ((wait_ret = waitpid(test_pid, &status, WUNTRACED | WCONTINUED | WNOHANG)) == 0) {
    const int64_t waited_msec = msec_since(start_time);
    if (timeout_msec && waited_msec > 0 && static_cast<uint64_t>(waited_msec) >= timeout_msec) {
      fprintf(stderr, "FAILURE: test did not finish within timeout of %" PRIu64 " milliseconds\n",
              timeout_msec);
      kill(test_pid, SIGKILL);
      return std::make_unique<Result>(test_name, TIMED_OUT, 0, waited_msec);
    }
    std::this_thread::sleep_for(kPollingInterval);
  }
  if (wait_ret == -1) {
    fprintf(stderr, "FAILURE: waitpid failed: %s\n", strerror(errno));
    return std::make_unique<Result>(test_name, FAILED_TO_WAIT, 0, 0);
  }
  if (WIFEXITED(status)) {
    int return_code = WEXITSTATUS(status);
    LaunchStatus launch_status = return_code ? FAILED_NONZERO_RETURN_CODE : SUCCESS;
    return std::make_unique<Result>(test_name, launch_status, return_code, msec_since(start_time));
  }
  if (WIFSIGNALED(status)) {
    fprintf(stderr, "FAILURE: test process killed by signal %d\n", WTERMSIG(status));
    return std::make_unique<Result>(test_name, FAILED_NONZERO_RETURN_CODE, 1, 0);
  }
  if (WIFSTOPPED(status)) {
    fprintf(stderr, "FAILURE: test process stopped by signal %d\n", WSTOPSIG(status));
    return std::make_unique<Result>(test_name, FAILED_NONZERO_RETURN_CODE, 1, 0);
  }

  fprintf(stderr, "FAILURE: test process with unexpected status: %#x", status);
  return std::make_unique<Result>(test_name, FAILED_UNKNOWN, 0, 0);
}

}  // namespace runtests
