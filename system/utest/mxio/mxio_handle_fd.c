// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <mxio/io.h>
#include <unittest/unittest.h>

bool epoll_test(void) {
    BEGIN_TEST;

    mx_handle_t h = MX_HANDLE_INVALID;
    ASSERT_EQ(NO_ERROR, mx_event_create(0u, &h), "mx_event_create() failed");
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
    ASSERT_EQ(NO_ERROR, mx_object_signal(h, 0u, MX_USER_SIGNAL_0),
              "mx_object_signal() failed");

    nfds = epoll_wait(epollfd, events, max_events, 0);
    EXPECT_EQ(nfds, 1, "");
    EXPECT_EQ(events[0].events, (uint32_t)EPOLLIN, "");

    // clear SIGNAL0 and set SIGNAL1
    ASSERT_EQ(NO_ERROR,
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
    ASSERT_EQ(NO_ERROR, mx_event_create(0u, &h), "mx_event_create() failed");
    ASSERT_GE(h, 0, "");

    // mxio_handle_fd() with shared_handle = true
    int fd = mxio_handle_fd(h, MX_USER_SIGNAL_0, MX_USER_SIGNAL_1, true);
    ASSERT_GT(fd, 0, "mxio_handle_fd() failed");

    close(fd);

    // close(fd) has not closed the wrapped handle
    EXPECT_EQ(NO_ERROR, mx_object_signal(h, 0, MX_USER_SIGNAL_0),
              "mx_object_signal() should succeed");

    // mxio_handle_fd() with shared_handle = false
    fd = mxio_handle_fd(h, MX_USER_SIGNAL_0, MX_USER_SIGNAL_1, false);
    ASSERT_GT(fd, 0, "mxio_handle_fd() failed");

    close(fd);

    // close(fd) has closed the wrapped handle
    EXPECT_EQ(ERR_BAD_HANDLE, mx_object_signal(h, 0, MX_USER_SIGNAL_0),
              "mx_object_signal() should fail");

    END_TEST;
}

BEGIN_TEST_CASE(mxio_handle_fd_test)
RUN_TEST(epoll_test);
RUN_TEST(close_test);
END_TEST_CASE(mxio_handle_fd_test)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
