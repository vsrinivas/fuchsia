// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// While not much will work if launchpad isn't already working, this test
// provides a place for testing aspects of launchpad that aren't necessarily
// normally used.

#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include <elfload/elfload.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>

#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>

#include <mxio/util.h>

#include <unittest/unittest.h>

#include "util.h"

static bool stdio_pipe_test(void)
{
    BEGIN_TEST;

    int fds[2];
    ASSERT_EQ(pipe(fds), 0, "pipe creation failed");

    ASSERT_GT(write(fds[1], "hello", 5), 0, "pipe write failed");

    char buffer[5];
    ASSERT_GT(read(fds[0], buffer, 5), 0, "pipe read failed");

    ASSERT_EQ(strncmp(buffer, "hello", 5), 0, "Incorrect buffer read from pipe");

    END_TEST;
}


static bool stdio_launchpad_pipe_test(void)
{
    BEGIN_TEST;

    // TODO(kulakowski): Consider another helper process
    const char* file = "/boot/bin/lsusb";
    launchpad_t* lp = NULL;

    mx_handle_t mxio_job = mx_job_default();
    ASSERT_GT(mxio_job, 0, "no mxio job object");

    mx_handle_t job_copy = MX_HANDLE_INVALID;
    ASSERT_EQ(mx_handle_duplicate(mxio_job, MX_RIGHT_SAME_RIGHTS, &job_copy),
              NO_ERROR, "mx_handle_duplicate failed");

    ASSERT_EQ(launchpad_create(job_copy,
                               "launchpad_pipe_stdio_test", &lp),
              NO_ERROR, "launchpad_create failed");
    ASSERT_EQ(launchpad_set_args(lp, 1, &file),
              NO_ERROR, "launchpad_arguments failed");
    ASSERT_EQ(launchpad_add_vdso_vmo(lp), NO_ERROR,
              "launchpad_add_vdso_vmo failed");
    ASSERT_EQ(launchpad_clone(lp, LP_CLONE_MXIO_ROOT | LP_CLONE_MXIO_CWD), NO_ERROR,
              "launchpad_clone failed");

    ASSERT_EQ(launchpad_elf_load(lp, launchpad_vmo_from_file(file)),
              NO_ERROR, "launchpad_elf_load failed");

    ASSERT_EQ(launchpad_load_vdso(lp, MX_HANDLE_INVALID),
              NO_ERROR, "launchpad_load_vdso failed");

    // stdio pipe fds [ours, theirs]
    int stdin_fds[2];
    int stdout_fds[2];
    int stderr_fds[2];

    ASSERT_EQ(stdio_pipe(stdin_fds, true), 0, "stdin pipe creation failed");
    ASSERT_EQ(stdio_pipe(stdout_fds, false), 0, "stdout pipe creation failed");
    ASSERT_EQ(stdio_pipe(stderr_fds, false), 0, "stderr pipe creation failed");

    // Transfer the child's stdio pipes
    ASSERT_EQ(launchpad_transfer_fd(lp, stdin_fds[1], 0), NO_ERROR,
              "failed to transfer stdin pipe to child process");
    ASSERT_EQ(launchpad_transfer_fd(lp, stdout_fds[1], 1), NO_ERROR,
              "failed to transfer stdout pipe to child process");
    ASSERT_EQ(launchpad_transfer_fd(lp, stderr_fds[1], 2), NO_ERROR,
              "failed to transfer stderr pipe to child process");

    // Start the process
    mx_handle_t p = launchpad_start(lp);
    ASSERT_GT(p, 0, "process handle > 0");

    // Read the stdio
    uint8_t* out = NULL;
    size_t out_size = 0;
    uint8_t* err = NULL;
    size_t err_size = 0;

    ASSERT_GE(read_to_end(stdout_fds[0], &out, &out_size), 0, "reading stdout failed");
    ASSERT_GE(read_to_end(stderr_fds[0], &err, &err_size), 0, "reading stderr failed");

    ASSERT_EQ(strncmp((char*)out, "ID   ", 5), 0, "Got wrong stdout");
    ASSERT_EQ(err_size, (size_t)0, "Got wrong stderr");

    free(out);
    free(err);

    close(stdin_fds[0]);
    close(stdout_fds[0]);
    close(stderr_fds[0]);

    // Wait for the process to finish
    mx_status_t r;

    r = mx_object_wait_one(p, MX_PROCESS_TERMINATED,
                           MX_TIME_INFINITE, NULL);
    ASSERT_EQ(r, NO_ERROR, "mx_object_wait_one failed");

    // read the return code
    mx_info_process_t proc_info;
    size_t actual = 0;
    mx_object_get_info(p, MX_INFO_PROCESS, &proc_info,
                       sizeof(proc_info), &actual, NULL);
    ASSERT_EQ(actual, (size_t)1, "Must get one and only one process info");
    ASSERT_EQ(proc_info.return_code, 0, "lsusb must return 0");

    mx_handle_close(p);
    launchpad_destroy(lp);

    END_TEST;
}

BEGIN_TEST_CASE(launchpad_tests)
RUN_TEST(stdio_pipe_test);
RUN_TEST(stdio_launchpad_pipe_test);
END_TEST_CASE(launchpad_tests)

int main(int argc, char **argv)
{
    bool success = unittest_run_all_tests(argc, argv);

    return success ? 0 : -1;
}
