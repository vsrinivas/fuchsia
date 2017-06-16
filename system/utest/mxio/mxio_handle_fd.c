// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <mxio/io.h>
#include <unittest/unittest.h>

bool epoll_test(void) {
    BEGIN_TEST;

    mx_handle_t h = MX_HANDLE_INVALID;
    ASSERT_EQ(MX_OK, mx_event_create(0u, &h), "mx_event_create() failed");
    ASSERT_GE(h, 0, "");

    int fd = mxio_handle_fd(h, MX_USER_SIGNAL_0, MX_USER_SIGNAL_1, false);
    ASSERT_GT(fd, 0, "mxio_handle_fd() failed");

    int epollfd = epoll_create(0);
    ASSERT_GT(fd, 0, "epoll_create() failed");

    int max_events = 1;
    struct epoll_event ev, events[max_events];
    ev.events = EPOLLIN | EPOLLOUT;

    ASSERT_EQ(0, epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev),
              "epoll_ctl() failed");

    int nfds = epoll_wait(epollfd, events, max_events, 0);
    EXPECT_EQ(nfds, 0, "");

    // set SIGNAL0
    ASSERT_EQ(MX_OK, mx_object_signal(h, 0u, MX_USER_SIGNAL_0),
              "mx_object_signal() failed");

    nfds = epoll_wait(epollfd, events, max_events, 0);
    EXPECT_EQ(nfds, 1, "");
    EXPECT_EQ(events[0].events, (uint32_t)EPOLLIN, "");

    // clear SIGNAL0 and set SIGNAL1
    ASSERT_EQ(MX_OK,
              mx_object_signal(h, MX_USER_SIGNAL_0, MX_USER_SIGNAL_1),
              "mx_object_signal() failed");

    nfds = epoll_wait(epollfd, events, max_events, 0);
    EXPECT_EQ(nfds, 1, "");
    EXPECT_EQ(events[0].events, (uint32_t)EPOLLOUT, "");

    close(fd);

    END_TEST;
}

bool close_test(void) {
    BEGIN_TEST;

    mx_handle_t h;
    ASSERT_EQ(MX_OK, mx_event_create(0u, &h), "mx_event_create() failed");
    ASSERT_GE(h, 0, "");

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
    ASSERT_GE(status, 0, "fcntl(F_GETFL) failed");

    status |= O_NONBLOCK;
    status = fcntl(fds[0], F_SETFL, status);
    ASSERT_GE(status, 0, "fcntl(FSETFL, O_NONBLOCK) failed");

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

BEGIN_TEST_CASE(mxio_handle_fd_test)
RUN_TEST(epoll_test);
RUN_TEST(close_test);
RUN_TEST(pipe_test);
END_TEST_CASE(mxio_handle_fd_test)
