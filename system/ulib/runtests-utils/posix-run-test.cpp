// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtests-utils/posix-run-test.h>

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fbl/auto_call.h>

namespace runtests {
namespace {

// Creates a deep copy of |argv|, assumed to be null-terminated.
char** CopyArgv(const char* const* argv) {
    int argc = 0;
    while(argv[argc] != nullptr){
      ++argc;
    }
    // allocate memory and copy strings
    char** argv_copy = static_cast<char**>(malloc((argc + 1) * sizeof(char*)));
    for (int i = 0; i < argc; ++i) {
        argv_copy[i] = static_cast<char*>(malloc(strlen(argv[i]) + 1));
        strcpy(argv_copy[i], argv[i]);
    }
    argv_copy[argc] = nullptr;
    return argv_copy;
}

} // namespace
Result PosixRunTest(const char* argv[], const char* output_filename) {
    int status;
    const char* path = argv[0];
    FILE* output_file = nullptr;

    // Becuase posix_spawn takes arguments for the subprocess of type
    // char* const*, it might mutate the values passed in: accordingly,
    // we pass in a copy of |argv| instead.
    char** argv_copy = CopyArgv(argv);

    // Initialize |file_actions|, which dictate what I/O will be performed in the
    // launched process, and ensure its destruction on function exit.
    posix_spawn_file_actions_t file_actions;
    if ((status = posix_spawn_file_actions_init(&file_actions))) {
        printf("FAILURE: posix_spawn_file_actions_init failed: %s\n",
               strerror(status));
        return Result(path, FAILED_TO_LAUNCH, 0);
    }

    auto auto_tidy = fbl::MakeAutoCall([&] {
        for (int i = 0; argv_copy[i] != nullptr; ++i) {
            free(argv_copy[i]);
        }
        free(argv_copy);

        posix_spawn_file_actions_destroy(&file_actions);
        if (output_file != nullptr) {
            fclose(output_file);
        }
    });

    // Tee output.
    if (output_filename != nullptr) {
        output_file = fopen(output_filename, "w");
        if (output_file == nullptr) {
            return Result(path, FAILED_DURING_IO, 0);
        }
        if ((status = posix_spawn_file_actions_addopen(
                 &file_actions, STDOUT_FILENO, output_filename,
                 O_WRONLY | O_CREAT | O_TRUNC, 0644))) {
            printf("FAILURE: posix_spawn_file_actions_addope failed: %s\n",
                   strerror(status));
            return Result(path, FAILED_TO_LAUNCH, 0);
        }
        if ((status = posix_spawn_file_actions_adddup2(&file_actions, STDOUT_FILENO,
                                                       STDERR_FILENO))) {
            printf("FAILURE: posix_spawn_file_actions_addup2 failed: %s\n",
                   strerror(status));
            return Result(path, FAILED_TO_LAUNCH, 0);
        }
    }

    // Launch the test subprocess.
    pid_t test_pid;
    if ((status = posix_spawn(&test_pid, path, &file_actions, nullptr, argv_copy, nullptr))) {
        printf("FAILURE: posix_spawn failed: %s\n", strerror(status));
        return Result(path, FAILED_TO_LAUNCH, 0);
    }

    if (waitpid(test_pid, &status, WUNTRACED | WCONTINUED) == -1) {
        printf("FAILURE: waitpid failed: %s\n", strerror(errno));
        return Result(path, FAILED_TO_WAIT, 0);
    }
    if (WIFEXITED(status)) {
        int return_code = WEXITSTATUS(status);
        LaunchStatus launch_status =
            return_code ? FAILED_NONZERO_RETURN_CODE : SUCCESS;
        return Result(path, launch_status, return_code);
    }
    if (WIFSIGNALED(status)) {
        printf("FAILURE: test process killed by signal %d\n", WTERMSIG(status));
        return Result(path, FAILED_NONZERO_RETURN_CODE, 1);
    }
    if (WIFSTOPPED(status)) {
        printf("FAILURE: test process stopped by signal %d\n", WSTOPSIG(status));
        return Result(path, FAILED_NONZERO_RETURN_CODE, 1);
    }

    printf("FAILURE: test process with unexpected status: %d", status);
    return Result(path, FAILED_UNKNOWN, 0);
}

} // namespace runtests
