// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtests-utils/posix-run-test.h>

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fbl/auto_call.h>
#include <unittest/unittest.h>

namespace runtests {
namespace {

// A whitelist of the names of environment variables names that we pass into the
// spawned test subprocess.
constexpr const char* const kEnvironmentWhitelist[] = {
    "TMPDIR",
    // Paths to the symbolizer for various sanitizers.
    "ASAN_SYMBOLIZER_PATH",
    "LSAN_SYMBOLIZER_PATH",
    "MSAN_SYMBOLIZER_PATH",
    "UBSAN_SYMBOLIZER_PATH",
    // From unittest.h. Set by RunAllTests().
    TEST_ENV_NAME,
};

} // namespace

fbl::unique_ptr<Result> PosixRunTest(const char* argv[],
                                     const char* output_filename) {
    int status;
    const char* path = argv[0];
    FILE* output_file = nullptr;

    // Initialize |file_actions|, which dictate what I/O will be performed in the
    // launched process, and ensure its destruction on function exit.
    posix_spawn_file_actions_t file_actions;
    if ((status = posix_spawn_file_actions_init(&file_actions))) {
        fprintf(stderr, "FAILURE: posix_spawn_file_actions_init failed: %s\n",
               strerror(status));
        return fbl::make_unique<Result>(path, FAILED_TO_LAUNCH, 0);
    }

    auto auto_tidy = fbl::MakeAutoCall([&] {
        posix_spawn_file_actions_destroy(&file_actions);
        if (output_file != nullptr) {
            fclose(output_file);
        }
    });

    // Construct the array of whitelisted environment variable strings of the
    // form "<name>=<value>".  The env_strings array just keeps the underlying
    // std::string objects alive so the envp pointers remain valid.
    std::string env_strings[countof(kEnvironmentWhitelist)];
    const char* envp[countof(env_strings) + 1];  // +1 for null terminator.
    size_t i = 0;
    for (const char* var : kEnvironmentWhitelist) {
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
            return fbl::make_unique<Result>(path, FAILED_DURING_IO, 0);
        }
        if ((status = posix_spawn_file_actions_addopen(
                 &file_actions, STDOUT_FILENO, output_filename,
                 O_WRONLY | O_CREAT | O_TRUNC, 0644))) {
            fprintf(stderr, "FAILURE: posix_spawn_file_actions_addopen failed: %s\n",
                   strerror(status));
            return fbl::make_unique<Result>(path, FAILED_TO_LAUNCH, 0);
        }
        if ((status = posix_spawn_file_actions_adddup2(&file_actions, STDOUT_FILENO,
                                                       STDERR_FILENO))) {
            fprintf(stderr, "FAILURE: posix_spawn_file_actions_adddup2 failed: %s\n",
                   strerror(status));
            return fbl::make_unique<Result>(path, FAILED_TO_LAUNCH, 0);
        }
    }

    // Launch the test subprocess.
    pid_t test_pid;
    if ((status = posix_spawn(&test_pid, path, &file_actions, nullptr,
                              const_cast<char**>(argv),
                              const_cast<char**>(envp)))) {
        fprintf(stderr, "FAILURE: posix_spawn failed: %s\n", strerror(status));
        return fbl::make_unique<Result>(path, FAILED_TO_LAUNCH, 0);
    }

    if (waitpid(test_pid, &status, WUNTRACED | WCONTINUED) == -1) {
        fprintf(stderr, "FAILURE: waitpid failed: %s\n", strerror(errno));
        return fbl::make_unique<Result>(path, FAILED_TO_WAIT, 0);
    }
    if (WIFEXITED(status)) {
        int return_code = WEXITSTATUS(status);
        LaunchStatus launch_status =
            return_code ? FAILED_NONZERO_RETURN_CODE : SUCCESS;
        return fbl::make_unique<Result>(path, launch_status, return_code);
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "FAILURE: test process killed by signal %d\n", WTERMSIG(status));
        return fbl::make_unique<Result>(path, FAILED_NONZERO_RETURN_CODE, 1);
    }
    if (WIFSTOPPED(status)) {
        fprintf(stderr, "FAILURE: test process stopped by signal %d\n", WSTOPSIG(status));
        return fbl::make_unique<Result>(path, FAILED_NONZERO_RETURN_CODE, 1);
    }

    fprintf(stderr, "FAILURE: test process with unexpected status: %#x", status);
    return fbl::make_unique<Result>(path, FAILED_UNKNOWN, 0);
}

} // namespace runtests
