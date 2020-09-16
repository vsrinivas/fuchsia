// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <elfload/elfload.h>
#include <test-utils/test-utils.h>
#include <zxtest/zxtest.h>

#include "util.h"

TEST(StdioTests, stdio_pipe_test) {
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
}

static zx_handle_t handle_from_fd(int fd) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t status = fdio_fd_transfer(fd, &handle);
  if (status != ZX_OK)
    tu_fatal("handle from fd", status);
  return handle;
}

TEST(StdioTests, stdio_advanced_pipe_test) {
  const char* file = "/pkg/bin/stdio-test-util";

  zx_handle_t fdio_job = zx_job_default();
  ASSERT_NE(fdio_job, ZX_HANDLE_INVALID, "no fdio job object");

  zx_handle_t job_copy = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_handle_duplicate(fdio_job, ZX_RIGHT_SAME_RIGHTS, &job_copy), ZX_OK,
            "zx_handle_duplicate failed");

  // stdio pipe fds [ours, theirs]
  int stdin_fds[2];
  int stdout_fds[2];
  int stderr_fds[2];

  ASSERT_EQ(stdio_pipe(stdin_fds, true), 0, "stdin pipe creation failed");
  ASSERT_EQ(stdio_pipe(stdout_fds, false), 0, "stdout pipe creation failed");
  ASSERT_EQ(stdio_pipe(stderr_fds, false), 0, "stderr pipe creation failed");

  zx_handle_t handles[] = {
      handle_from_fd(stdin_fds[1]),
      handle_from_fd(stdout_fds[1]),
      handle_from_fd(stderr_fds[1]),
  };
  uint32_t handle_ids[] = {
      PA_HND(PA_FD, 0),
      PA_HND(PA_FD, 1),
      PA_HND(PA_FD, 2),
  };

  // Start the process
  zx_handle_t p = tu_launch_process(job_copy, "pipe_stdio_test", 1, &file, 0, NULL,
                                    countof(handles), handles, handle_ids);
  ASSERT_NE(p, ZX_HANDLE_INVALID, "process handle != 0");

  // Read the stdio
  uint8_t* out = NULL;
  size_t out_size = 0;
  uint8_t* err = NULL;
  size_t err_size = 0;

  ASSERT_GE(read_to_end(stdout_fds[0], &out, &out_size), 0, "reading stdout failed");
  ASSERT_GE(read_to_end(stderr_fds[0], &err, &err_size), 0, "reading stderr failed");

  ASSERT_EQ(strncmp((char*)out, "Hello universe!", 15), 0, "Got wrong stdout");
  ASSERT_EQ(err_size, (size_t)0, "Got wrong stderr");

  free(out);
  free(err);

  close(stdin_fds[0]);
  close(stdout_fds[0]);
  close(stderr_fds[0]);

  // Wait for the process to finish
  zx_status_t r;

  r = zx_object_wait_one(p, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE, NULL);
  ASSERT_EQ(r, ZX_OK, "zx_object_wait_one failed");

  // read the return code
  zx_info_process_t proc_info;
  size_t actual = 0;
  zx_object_get_info(p, ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), &actual, NULL);
  ASSERT_EQ(actual, (size_t)1, "Must get one and only one process info");
  ASSERT_EQ(proc_info.return_code, 0, "lsusb must return 0");

  zx_handle_close(p);
}

typedef struct ThreadData {
  FILE* f;
  size_t index;
} ThreadData;

static void* thread_func_do_some_printing(void* arg) {
  ThreadData* data = (ThreadData*)arg;
  for (int i = 0; i < 100; ++i) {
    fprintf(data->f, "this is message %d from thread %zu\n", i, data->index);
  }
  return NULL;
}

// This is a crash regression test, multithreaded access to FILE* was racy and
// could crash. If this test is "flaky", this has regressed. See ZX-4278.
TEST(StdioTests, stdio_race_on_file_access) {
  zx_time_t start_time = zx_clock_get_monotonic();
  while (zx_clock_get_monotonic() - start_time < ZX_SEC(5)) {
    FILE* f = tmpfile();
    ASSERT_NOT_NULL(f, "tmpfile failed");

    pthread_t threads[100];
    ThreadData thread_data[countof(threads)];

    for (size_t i = 0; i < countof(threads); ++i) {
      ThreadData* data = &thread_data[i];
      data->f = f;
      data->index = i;

      int err = pthread_create(&threads[i], NULL, thread_func_do_some_printing, data);
      ASSERT_EQ(err, 0, "pthread_create");
    }

    for (size_t i = 0; i < countof(threads); ++i) {
      int err = pthread_join(threads[i], NULL);
      ASSERT_EQ(err, 0, "pthread_join");
    }

    fclose(f);
  }
}
