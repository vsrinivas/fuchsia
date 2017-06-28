// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

#include <unittest/unittest.h>

bool socketpair_test(void) {
    BEGIN_TEST;

    int fds[2];
    int status = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    ASSERT_EQ(status, 0, "socketpair(AF_UNIX, SOCK_STREAM, 0, fds) failed");

    char buf[4] = "abc\0";
    status = write(fds[0], buf, 4);
    if (status < 0) printf("errno=%s\n", strerror(errno));
    ASSERT_EQ(status, 4, "write failed");

    char recvbuf[4];
    status = read(fds[1], recvbuf, 4);
    if (status < 0) printf("errno=%s\n", strerror(errno));
    ASSERT_EQ(status, 4, "read failed");

    ASSERT_EQ(memcmp(buf, recvbuf, 4), 0, "data did not make it");

    ASSERT_EQ(close(fds[0]), 0, "close(fds[0]) failed");
    ASSERT_EQ(close(fds[1]), 0, "close(fds[1]) failed");

    END_TEST;
}

BEGIN_TEST_CASE(mxio_socketpair_test)
RUN_TEST(socketpair_test);
END_TEST_CASE(mxio_socketpair_test)
