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

#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <fdio/util.h>

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

    ASSERT_EQ(lseek(fds[0], 0, SEEK_SET), -1, "lseek should have failed");
    ASSERT_EQ(errno, ESPIPE, "lseek error should have been pipe-related");

    ASSERT_EQ(close(fds[0]), 0, "");
    ASSERT_EQ(close(fds[1]), 0, "");

    END_TEST;
}


static bool stdio_launchpad_pipe_test(void)
{
    BEGIN_TEST;

    // TODO(kulakowski): Consider another helper process
    const char* file = "/boot/bin/lsusb";
    launchpad_t* lp = NULL;

    zx_handle_t fdio_job = zx_job_default();
    ASSERT_NE(fdio_job, ZX_HANDLE_INVALID, "no fdio job object");

    zx_handle_t job_copy = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_handle_duplicate(fdio_job, ZX_RIGHT_SAME_RIGHTS, &job_copy),
              ZX_OK, "zx_handle_duplicate failed");

    ASSERT_EQ(launchpad_create(job_copy,
                               "launchpad_pipe_stdio_test", &lp),
              ZX_OK, "launchpad_create failed");
    ASSERT_EQ(launchpad_set_args(lp, 1, &file),
              ZX_OK, "launchpad_arguments failed");
    ASSERT_EQ(launchpad_add_vdso_vmo(lp), ZX_OK,
              "launchpad_add_vdso_vmo failed");
    ASSERT_EQ(launchpad_clone(lp, LP_CLONE_FDIO_NAMESPACE),
              ZX_OK, "launchpad_clone failed");

    zx_handle_t vmo;
    ASSERT_EQ(launchpad_vmo_from_file(file, &vmo), ZX_OK, "");
    ASSERT_EQ(launchpad_elf_load(lp, vmo),
              ZX_OK, "launchpad_elf_load failed");

    ASSERT_EQ(launchpad_load_vdso(lp, ZX_HANDLE_INVALID),
              ZX_OK, "launchpad_load_vdso failed");

    // stdio pipe fds [ours, theirs]
    int stdin_fds[2];
    int stdout_fds[2];
    int stderr_fds[2];

    ASSERT_EQ(stdio_pipe(stdin_fds, true), 0, "stdin pipe creation failed");
    ASSERT_EQ(stdio_pipe(stdout_fds, false), 0, "stdout pipe creation failed");
    ASSERT_EQ(stdio_pipe(stderr_fds, false), 0, "stderr pipe creation failed");

    // Transfer the child's stdio pipes
    ASSERT_EQ(launchpad_transfer_fd(lp, stdin_fds[1], 0), ZX_OK,
              "failed to transfer stdin pipe to child process");
    ASSERT_EQ(launchpad_transfer_fd(lp, stdout_fds[1], 1), ZX_OK,
              "failed to transfer stdout pipe to child process");
    ASSERT_EQ(launchpad_transfer_fd(lp, stderr_fds[1], 2), ZX_OK,
              "failed to transfer stderr pipe to child process");

    // Start the process
    zx_handle_t p = ZX_HANDLE_INVALID;
    zx_status_t status = launchpad_go(lp, &p, NULL);
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_NE(p, ZX_HANDLE_INVALID, "process handle != 0");

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
    zx_status_t r;

    r = zx_object_wait_one(p, ZX_PROCESS_TERMINATED,
                           ZX_TIME_INFINITE, NULL);
    ASSERT_EQ(r, ZX_OK, "zx_object_wait_one failed");

    // read the return code
    zx_info_process_t proc_info;
    size_t actual = 0;
    zx_object_get_info(p, ZX_INFO_PROCESS, &proc_info,
                       sizeof(proc_info), &actual, NULL);
    ASSERT_EQ(actual, (size_t)1, "Must get one and only one process info");
    ASSERT_EQ(proc_info.return_code, 0, "lsusb must return 0");

    zx_handle_close(p);

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
