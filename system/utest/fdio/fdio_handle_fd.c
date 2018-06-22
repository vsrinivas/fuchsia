// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <threads.h>
#include <unistd.h>

#include <lib/fdio/io.h>
#include <lib/fdio/util.h>
#include <unittest/unittest.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

bool close_test(void) {
    BEGIN_TEST;

    zx_handle_t h = ZX_HANDLE_INVALID;
    ASSERT_EQ(ZX_OK, zx_event_create(0u, &h), "zx_event_create() failed");
    ASSERT_NE(h, ZX_HANDLE_INVALID, "");

    // fdio_handle_fd() with shared_handle = true
    int fd = fdio_handle_fd(h, ZX_USER_SIGNAL_0, ZX_USER_SIGNAL_1, true);
    ASSERT_GT(fd, 0, "fdio_handle_fd() failed");

    close(fd);

    // close(fd) has not closed the wrapped handle
    EXPECT_EQ(ZX_OK, zx_object_signal(h, 0, ZX_USER_SIGNAL_0),
              "zx_object_signal() should succeed");

    // fdio_handle_fd() with shared_handle = false
    fd = fdio_handle_fd(h, ZX_USER_SIGNAL_0, ZX_USER_SIGNAL_1, false);
    ASSERT_GT(fd, 0, "fdio_handle_fd() failed");

    close(fd);

    // close(fd) has closed the wrapped handle
    EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_object_signal(h, 0, ZX_USER_SIGNAL_0),
              "zx_object_signal() should fail");

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

int write_thread(void* arg) {
    // Sleep to try to ensure the write happens after the poll.
    zx_nanosleep(ZX_MSEC(5));
    int message[2] = {-6, 1};
    ssize_t written = write(*(int*)arg, message, sizeof(message));
    ASSERT_GE(written, 0, "write() failed");
    ASSERT_EQ((uint32_t)written, sizeof(message),
              "write() should have written the whole message.");
    return 0;
}

bool ppoll_test_handler(struct timespec* timeout) {
    BEGIN_TEST;

    int fds[2];
    int status = pipe(fds);
    ASSERT_EQ(status, 0, "pipe() failed");

    thrd_t t;
    int thrd_create_result = thrd_create(&t, write_thread, &fds[1]);
    ASSERT_EQ(thrd_create_result, thrd_success, "create blocking send thread");

    struct pollfd poll_fds[1] = {{fds[0], POLLIN, 0}};
    int ppoll_result = ppoll(poll_fds, 1, timeout, NULL);

    EXPECT_EQ(1, ppoll_result, "didn't read anything");

    ASSERT_EQ(thrd_join(t, NULL), thrd_success, "join blocking send thread");

    END_TEST;
}

bool ppoll_negative_test(void) {
    struct timespec timeout_ts = {-1, -1};
    return ppoll_test_handler(&timeout_ts);
}

bool ppoll_null_test(void) {
    return ppoll_test_handler(NULL);
}

bool ppoll_overflow_test(void) {
    unsigned int nanoseconds_in_seconds = 1000000000;
    struct timespec timeout_ts = {UINT64_MAX / nanoseconds_in_seconds, UINT64_MAX % nanoseconds_in_seconds};
    return ppoll_test_handler(&timeout_ts);
}

bool ppoll_immediate_timeout_test(void) {
    BEGIN_TEST;

    int fds[2];
    int status = pipe(fds);
    ASSERT_EQ(status, 0, "pipe() failed");

    struct timespec timeout = {0, 0};
    struct pollfd poll_fds[1] = {{fds[0], POLLIN, 0}};
    int ppoll_result = ppoll(poll_fds, 1, &timeout, NULL);

    EXPECT_EQ(0, ppoll_result, "no fds should be readable");

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
    zx_handle_t handles[FDIO_MAX_HANDLES];
    uint32_t types[FDIO_MAX_HANDLES];
    zx_status_t r = fdio_transfer_fd(fds[0], 0, handles, types);
    ASSERT_GT(r, 0, "failed to transfer fds to handles");

    // handles --> fd
    ASSERT_EQ(fdio_create_fd(handles, types, r, &fds[0]), ZX_OK,
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

bool transfer_device_test(void) {
    BEGIN_TEST;

    int fd = open("/dev/zero", O_RDONLY);
    ASSERT_GE(fd, 0, "Failed to open /dev/zero");

    // fd --> handles
    zx_handle_t handles[FDIO_MAX_HANDLES];
    uint32_t types[FDIO_MAX_HANDLES];
    zx_status_t r = fdio_transfer_fd(fd, 0, handles, types);
    ASSERT_GT(r, 0, "failed to transfer fds to handles");

    // handles --> fd
    ASSERT_EQ(fdio_create_fd(handles, types, r, &fd), ZX_OK,
              "failed to transfer handles to fds");

    ASSERT_EQ(close(fd), 0, "Failed to close fd");

    END_TEST;
}

bool create_fd_from_connected_socket(void) {
    BEGIN_TEST;

    int fd;
    uint32_t type = PA_FDIO_SOCKET;
    zx_handle_t h1, h2;
    ASSERT_EQ(ZX_OK, zx_socket_create(ZX_SOCKET_STREAM, &h1, &h2),
              "failed to create socket pair");
    ASSERT_EQ(ZX_OK, fdio_create_fd(&h1, &type, 1, &fd),
              "failed to create FD for socket handle");

    int message[2] = {0xab, 0x1234};
    size_t written;
    ASSERT_EQ(ZX_OK, zx_socket_write(h2, 0, message, sizeof(message), &written),
              "failed to write to socket handle");
    ASSERT_EQ(sizeof(message), written,
              "failed to write full message to socket handle");

    int read_message[2] = {};
    ssize_t bytes_read = read(fd, read_message, sizeof(read_message));
    ASSERT_EQ(sizeof(message), (uint32_t)bytes_read,
              "failed to read from socket fd");
    ASSERT_EQ(0, memcmp(message, read_message, sizeof(message)),
              "incorrect bytes read from socket fd");

    END_TEST;
}

BEGIN_TEST_CASE(fdio_handle_fd_test)
RUN_TEST(close_test);
RUN_TEST(pipe_test);
RUN_TEST(ppoll_negative_test);
RUN_TEST(ppoll_null_test);
RUN_TEST(ppoll_overflow_test);
RUN_TEST(ppoll_immediate_timeout_test);
RUN_TEST(transfer_fd_test);
RUN_TEST(transfer_device_test);
RUN_TEST(create_fd_from_connected_socket);
END_TEST_CASE(fdio_handle_fd_test)
