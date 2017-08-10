// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <mxio/io.h>
#include <mxio/util.h>
#include <unittest/unittest.h>

bool close_test(void) {
    BEGIN_TEST;

    mx_handle_t h = MX_HANDLE_INVALID;
    ASSERT_EQ(MX_OK, mx_event_create(0u, &h), "mx_event_create() failed");
    ASSERT_NE(h, MX_HANDLE_INVALID, "");

    // mxio_handle_fd() with shared_handle = true
    int fd = mxio_handle_fd(h, MX_USER_SIGNAL_0, MX_USER_SIGNAL_1, true);
    ASSERT_GT(fd, 0, "mxio_handle_fd() failed");

    close(fd);

    // close(fd) has not closed the wrapped handle
    EXPECT_EQ(MX_OK, mx_object_signal(h, 0, MX_USER_SIGNAL_0),
              "mx_object_signal() should succeed");

    // mxio_handle_fd() with shared_handle = false
    fd = mxio_handle_fd(h, MX_USER_SIGNAL_0, MX_USER_SIGNAL_1, false);
    ASSERT_GT(fd, 0, "mxio_handle_fd() failed");

    close(fd);

    // close(fd) has closed the wrapped handle
    EXPECT_EQ(MX_ERR_BAD_HANDLE, mx_object_signal(h, 0, MX_USER_SIGNAL_0),
              "mx_object_signal() should fail");

    END_TEST;
}

bool pipe_test(void) {
    BEGIN_TEST;

    int fds[2];
    int status = pipe(fds);
    ASSERT_EQ(status, 0, "pipe() failed");

    status = fcntl(fds[0], F_GETFL);
    ASSERT_EQ(status, 0, "fcntl(F_GETFL) failed");

    status |= O_NONBLOCK;
    status = fcntl(fds[0], F_SETFL, status);
    ASSERT_EQ(status, 0, "fcntl(FSETFL, O_NONBLOCK) failed");

    status = fcntl(fds[0], F_GETFL);
    ASSERT_EQ(status, O_NONBLOCK, "fcntl(F_GETFL) failed");

    int message[2] = {-6, 1};
    ssize_t written = write(fds[1], message, sizeof(message));
    ASSERT_GE(written, 0, "write() failed");
    ASSERT_EQ((uint32_t)written, sizeof(message),
              "write() should have written the whole message.");

    int available = 0;
    status = ioctl(fds[0], FIONREAD, &available);
    ASSERT_GE(status, 0, "ioctl(FIONREAD) failed");
    EXPECT_EQ((uint32_t)available, sizeof(message),
              "ioctl(FIONREAD) queried wrong number of bytes");

    int read_message[2];
    ssize_t bytes_read = read(fds[0], read_message, sizeof(read_message));
    ASSERT_GE(bytes_read, 0, "read() failed");
    ASSERT_EQ((uint32_t)bytes_read, sizeof(read_message),
              "read() read wrong number of bytes");

    EXPECT_EQ(read_message[0], message[0], "read() read wrong value");
    EXPECT_EQ(read_message[1], message[1], "read() read wrong value");

    END_TEST;
}

bool transfer_fd_test(void) {
    BEGIN_TEST;

    int fds[2];
    int status = pipe(fds);
    ASSERT_EQ(status, 0, "pipe() failed");

    // Make pipe nonblocking, write message
    status |= O_NONBLOCK;
    status = fcntl(fds[0], F_SETFL, status);
    ASSERT_EQ(status, 0, "fcntl(FSETFL, O_NONBLOCK) failed");
    int message[2] = {-6, 1};
    ssize_t written = write(fds[1], message, sizeof(message));
    ASSERT_GE(written, 0, "write() failed");
    ASSERT_EQ((uint32_t)written, sizeof(message),
              "write() should have written the whole message.");


    // fd --> handles
    mx_handle_t handles[MXIO_MAX_HANDLES];
    uint32_t types[MXIO_MAX_HANDLES];
    mx_status_t r = mxio_transfer_fd(fds[0], 0, handles, types);
    ASSERT_GT(r, 0, "failed to transfer fds to handles");

    // handles --> fd
    ASSERT_EQ(mxio_create_fd(handles, types, r, &fds[0]), MX_OK,
              "failed to transfer handles to fds");

    // Read message
    int read_message[2];
    ssize_t bytes_read = read(fds[0], read_message, sizeof(read_message));
    ASSERT_GE(bytes_read, 0, "read() failed");
    ASSERT_EQ((uint32_t)bytes_read, sizeof(read_message),
              "read() read wrong number of bytes");

    EXPECT_EQ(read_message[0], message[0], "read() read wrong value");
    EXPECT_EQ(read_message[1], message[1], "read() read wrong value");

    END_TEST;
}

BEGIN_TEST_CASE(mxio_handle_fd_test)
RUN_TEST(close_test);
RUN_TEST(pipe_test);
RUN_TEST(transfer_fd_test);
END_TEST_CASE(mxio_handle_fd_test)
