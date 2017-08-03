// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <unittest/unittest.h>

bool socketpair_test(void) {
    BEGIN_TEST;

    int fds[2];
    int status = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    ASSERT_EQ(status, 0, "socketpair(AF_UNIX, SOCK_STREAM, 0, fds) failed");

    // write() and read() should work.
    char buf[4] = "abc\0";
    status = write(fds[0], buf, 4);
    if (status < 0) printf("write failed %s\n", strerror(errno));
    EXPECT_EQ(status, 4, "write failed");

    char recvbuf[4];
    status = read(fds[1], recvbuf, 4);
    if (status < 0) printf("read failed %s\n", strerror(errno));
    EXPECT_EQ(status, 4, "read failed");

    EXPECT_EQ(memcmp(buf, recvbuf, 4), 0, "data did not make it after write+read");

    // send() and recv() should also work.
    memcpy(buf, "def", 4);
    status = send(fds[1], buf, 4, 0);
    if (status < 0) printf("send failed %s\n", strerror(errno));
    EXPECT_EQ(status, 4, "send failed");

    status = recv(fds[0], recvbuf, 4, 0);
    if (status < 0) printf("recv failed %s\n", strerror(errno));

    EXPECT_EQ(memcmp(buf, recvbuf, 4), 0, "data did not make it after send+recv");

    EXPECT_EQ(close(fds[0]), 0, "close(fds[0]) failed");
    EXPECT_EQ(close(fds[1]), 0, "close(fds[1]) failed");

    END_TEST;
}

static_assert(EAGAIN == EWOULDBLOCK, "Assuming EAGAIN and EWOULDBLOCK have same value");

bool socketpair_shutdown_setup(int fds[2]) {
    int status = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    ASSERT_EQ(status, 0, "socketpair(AF_UNIX, SOCK_STREAM, 0, fds) failed");

    // Set both ends to non-blocking to make testing for readability/writability easier.
    ASSERT_EQ(fcntl(fds[0], F_SETFL, O_NONBLOCK), 0, "");
    ASSERT_EQ(fcntl(fds[1], F_SETFL, O_NONBLOCK), 0, "");

    char buf[1] = {};
    // Both sides should be readable.
    errno = 0;
    status = read(fds[0], buf, sizeof(buf));
    EXPECT_EQ(status, -1, "fds[0] should initially be readable");
    EXPECT_EQ(errno, EAGAIN, "");
    errno = 0;
    status = read(fds[1], buf, sizeof(buf));
    EXPECT_EQ(status, -1, "fds[1] should initially be readable");
    EXPECT_EQ(errno, EAGAIN, "");

    // Both sides should be writable.
    EXPECT_EQ(write(fds[0], buf, sizeof(buf)), 1, "fds[0] should be initially writable");
    EXPECT_EQ(write(fds[1], buf, sizeof(buf)), 1, "fds[1] should be initially writable");

    EXPECT_EQ(read(fds[0], buf, sizeof(buf)), 1, "");
    EXPECT_EQ(read(fds[1], buf, sizeof(buf)), 1, "");

    return true;
}

bool socketpair_shutdown_rd_test(void) {
    BEGIN_TEST;

    int fds[2];
    socketpair_shutdown_setup(fds);

    // Write a byte into fds[1] to test for readability later.
    char buf[1] = {};
    EXPECT_EQ(write(fds[1], buf, sizeof(buf)), 1, "");

    // Close one side down for reading.
    int status = shutdown(fds[0], SHUT_RD);
    EXPECT_EQ(status, 0, "shutdown(fds[0], SHUT_RD)");
    if (status != 0)
        printf("\nerrno %d\n", errno);

    /*
    TODO: doesn't pass on linux, maybe there's another way to test for SHUTDOWN_RD ?
    // Shouldn't be able to read from fds[0] any more.
    errno = 0;
    EXPECT_EQ(read(fds[0], buf, sizeof(buf)), -1, "fds[0] should not be readable after SHUT_RD");
    EXPECT_EQ(errno, EINVAL, "read should return EINVAL after shutdown(SHUT_RD)");
    */

    EXPECT_EQ(close(fds[0]), 0, "");
    EXPECT_EQ(close(fds[1]), 0, "");

    END_TEST;
}

#if defined(__Fuchsia__)
#define SEND_FLAGS 0
#else
#define SEND_FLAGS MSG_NOSIGNAL
#endif

bool socketpair_shutdown_wr_test(void) {
    BEGIN_TEST;

    int fds[2];
    socketpair_shutdown_setup(fds);

    // Close one side down for writing.
    int status = shutdown(fds[0], SHUT_WR);
    EXPECT_EQ(status, 0, "shutdown(fds[0], SHUT_WR)");
    if (status != 0)
        printf("\nerrno %d\n", errno);

    char buf[1] = {};

    // Should still be readable.
    EXPECT_EQ(read(fds[0], buf, sizeof(buf)), -1, "");
    EXPECT_EQ(errno, EAGAIN, "errno after read after SHUT_WR");

    // But not writable
    EXPECT_EQ(send(fds[0], buf, sizeof(buf), SEND_FLAGS), -1, "write after SHUT_WR");
    EXPECT_EQ(errno, EPIPE, "errno after write after SHUT_WR");

    // Should still be able to write + read a message in the other direction.
    EXPECT_EQ(write(fds[1], buf, sizeof(buf)), 1, "");
    EXPECT_EQ(read(fds[0], buf, sizeof(buf)), 1, "");

    EXPECT_EQ(close(fds[0]), 0, "");
    EXPECT_EQ(close(fds[1]), 0, "");

    END_TEST;
}

bool socketpair_shutdown_rdwr_test(void) {
    BEGIN_TEST;

    int fds[2];
    socketpair_shutdown_setup(fds);

    // Close one side for reading and writing.
    int status = shutdown(fds[0], SHUT_RDWR);
    EXPECT_EQ(status, 0, "shutdown(fds[0], SHUT_RDWR");
    if (status != 0)
        printf("\nerrno %d\n", errno);

    char buf[1] = {};

    /*
    TODO: doesn't pass on linux, maybe there's another way to test for SHUTDOWN_RD ?
    // Reading should fail.
    EXPECT_EQ(read(fds[0], buf, sizeof(buf)), -1, "");
    EXPECT_EQ(errno, EAGAIN, "errno after read after SHUT_RDWR");
    */

    // Writing should fail.
    EXPECT_EQ(send(fds[0], buf, sizeof(buf), SEND_FLAGS), -1, "");
    EXPECT_EQ(errno, EPIPE, "errno after write after SHUT_RDWR");

    END_TEST;
}

BEGIN_TEST_CASE(mxio_socketpair_test)
RUN_TEST(socketpair_test);
RUN_TEST(socketpair_shutdown_rd_test);
RUN_TEST(socketpair_shutdown_wr_test);
RUN_TEST(socketpair_shutdown_rdwr_test);
END_TEST_CASE(mxio_socketpair_test)
