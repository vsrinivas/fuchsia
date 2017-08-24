// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/syscalls.h>

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

#if defined(__Fuchsia__)
#define SEND_FLAGS 0
#else
#define SEND_FLAGS MSG_NOSIGNAL
#endif


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

    // Can read the byte already written into the pipe.
    EXPECT_EQ(read(fds[0], buf, sizeof(buf)), 1, "fds[0] should not be readable after SHUT_RD");

    // But not send any further bytes
    EXPECT_EQ(send(fds[1], buf, sizeof(buf), SEND_FLAGS), -1, "");
    EXPECT_EQ(errno, EPIPE, "send should return EPIPE after shutdown(SHUT_RD) on other side");

    // Or read any more
    EXPECT_EQ(read(fds[0], buf, sizeof(buf)), 0, "");

    EXPECT_EQ(close(fds[0]), 0, "");
    EXPECT_EQ(close(fds[1]), 0, "");

    END_TEST;
}

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

    // Writing should fail.
    EXPECT_EQ(send(fds[0], buf, sizeof(buf), SEND_FLAGS), -1, "");
    EXPECT_EQ(errno, EPIPE, "errno after write after SHUT_RDWR");

    // Reading should return no data.
    EXPECT_EQ(read(fds[0], buf, sizeof(buf)), 0, "");

    END_TEST;
}

typedef struct poll_for_read_args {
    int fd;
    int poll_result;
    mx_time_t poll_time;
} poll_for_read_args_t;

int poll_for_read_with_timeout(void* arg) {
    poll_for_read_args_t* poll_args = (poll_for_read_args_t*) arg;
    struct pollfd pollfd;
    pollfd.fd = poll_args->fd;
    pollfd.events = POLLIN;
    pollfd.revents = 0;

    int timeout_ms = 100;
    mx_time_t time_before = mx_time_get(CLOCK_MONOTONIC);
    poll_args->poll_result = poll(&pollfd, 1, timeout_ms);
    mx_time_t time_after = mx_time_get(CLOCK_MONOTONIC);
    poll_args->poll_time = time_after - time_before;

    int num_readable = 0;
    EXPECT_EQ(ioctl(poll_args->fd, FIONREAD, &num_readable), 0, "ioctl(FIONREAD)");
    EXPECT_EQ(num_readable, 0, "");

    return 0;
}


bool socketpair_shutdown_self_wr_poll_test(void) {
    BEGIN_TEST;

    int fds[2];
    socketpair_shutdown_setup(fds);

    poll_for_read_args_t poll_args = {};
    poll_args.fd = fds[0];
    thrd_t poll_thread;
    int thrd_create_result = thrd_create(&poll_thread, poll_for_read_with_timeout, &poll_args);
    ASSERT_EQ(thrd_create_result, thrd_success, "create blocking read thread");

    shutdown(fds[0], SHUT_RDWR);

    ASSERT_EQ(thrd_join(poll_thread, NULL), thrd_success, "join blocking read thread");

    EXPECT_EQ(poll_args.poll_result, 1, "poll should have one entry");
    EXPECT_LT(poll_args.poll_time, 100u * 1000 * 1000, "poll should not have timed out");

    END_TEST;
}

bool socketpair_shutdown_peer_wr_poll_test(void) {
    BEGIN_TEST;

    int fds[2];
    socketpair_shutdown_setup(fds);

    poll_for_read_args_t poll_args = {};
    poll_args.fd = fds[0];
    thrd_t poll_thread;
    int thrd_create_result = thrd_create(&poll_thread, poll_for_read_with_timeout, &poll_args);
    ASSERT_EQ(thrd_create_result, thrd_success, "create blocking read thread");

    shutdown(fds[1], SHUT_RDWR);

    ASSERT_EQ(thrd_join(poll_thread, NULL), thrd_success, "join blocking read thread");

    EXPECT_EQ(poll_args.poll_result, 1, "poll should have one entry");
    EXPECT_LT(poll_args.poll_time, 100u * 1000 * 1000, "poll should not have timed out");

    END_TEST;
}

#define BUF_SIZE 256

typedef struct recv_args {
    int fd;
    int recv_result;
    int recv_errno;
    char buf[BUF_SIZE];
} recv_args_t;

int recv_thread(void* arg) {
    recv_args_t* recv_args = (recv_args_t*) arg;

    recv_args->recv_result = recv(recv_args->fd, recv_args->buf, BUF_SIZE, 0u);
    if (recv_args->recv_result < 0)
        recv_args->recv_errno = errno;

    return 0;
}

bool socketpair_shutdown_self_rd_during_recv_test(void) {
    BEGIN_TEST;

    int fds[2];
    int status = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    ASSERT_EQ(status, 0, "socketpair(AF_UNIX, SOCK_STREAM, 0, fds) failed");

    recv_args_t recv_args = {};
    recv_args.fd = fds[0];
    thrd_t t;
    int thrd_create_result = thrd_create(&t, recv_thread, &recv_args);
    ASSERT_EQ(thrd_create_result, thrd_success, "create blocking read thread");

    shutdown(fds[0], SHUT_RD);

    ASSERT_EQ(thrd_join(t, NULL), thrd_success, "join blocking read thread");

    EXPECT_EQ(recv_args.recv_result, 0, "recv should have returned 0");
    EXPECT_EQ(recv_args.recv_errno, 0, "recv should have left errno alone");

    END_TEST;
}


bool socketpair_shutdown_peer_wr_during_recv_test(void) {
    BEGIN_TEST;

    int fds[2];
    int status = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    ASSERT_EQ(status, 0, "socketpair(AF_UNIX, SOCK_STREAM, 0, fds) failed");

    recv_args_t recv_args = {};
    recv_args.fd = fds[0];
    thrd_t t;
    int thrd_create_result = thrd_create(&t, recv_thread, &recv_args);
    ASSERT_EQ(thrd_create_result, thrd_success, "create blocking read thread");

    shutdown(fds[1], SHUT_WR);

    ASSERT_EQ(thrd_join(t, NULL), thrd_success, "join blocking read thread");

    EXPECT_EQ(recv_args.recv_result, 0, "recv should have returned 0");
    EXPECT_EQ(recv_args.recv_errno, 0, "recv should have left errno alone");
    END_TEST;
}

typedef struct send_args {
    int fd;
    int send_result;
    int send_errno;
    char buf[BUF_SIZE];
} send_args_t;

int send_thread(void* arg) {
    send_args_t* send_args = (send_args_t*) arg;

    send_args->send_result = send(send_args->fd, send_args->buf, BUF_SIZE, 0u);
    if (send_args->send_result < 0)
        send_args->send_errno = errno;

    return 0;
}

bool socketpair_shutdown_self_wr_during_send_test(void) {
    BEGIN_TEST;

    int fds[2];
    int status = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    ASSERT_EQ(status, 0, "socketpair(AF_UNIX, SOCK_STREAM, 0, fds) failed");

    // First, fill up the socket so the next send() will block.
    char buf[BUF_SIZE] = {};
    while (true) {
      status = send(fds[0], buf, sizeof(buf), MSG_DONTWAIT);
      if (status < 0) {
          ASSERT_EQ(errno, EAGAIN, "send should eventually return EAGAIN when full");
          break;
      }
    }
    send_args_t send_args = {};
    send_args.fd = fds[0];
    thrd_t t;
    // Then start a thread blocking on a send().
    int thrd_create_result = thrd_create(&t, send_thread, &send_args);
    ASSERT_EQ(thrd_create_result, thrd_success, "create blocking send thread");

    shutdown(fds[0], SHUT_WR);

    ASSERT_EQ(thrd_join(t, NULL), thrd_success, "join blocking send thread");

    EXPECT_EQ(send_args.send_result, -1, "send should have returned -1");
    EXPECT_EQ(send_args.send_errno, EPIPE, "send should have set errno to EPIPE");

    END_TEST;
}

bool socketpair_shutdown_peer_rd_during_send_test(void) {
    BEGIN_TEST;

    int fds[2];
    int status = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    ASSERT_EQ(status, 0, "socketpair(AF_UNIX, SOCK_STREAM, 0, fds) failed");

    // First, fill up the socket so the next send() will block.
    char buf[BUF_SIZE] = {};
    while (true) {
      status = send(fds[0], buf, sizeof(buf), MSG_DONTWAIT);
      if (status < 0) {
          ASSERT_EQ(errno, EAGAIN, "send should eventually return EAGAIN when full");
          break;
      }
    }
    send_args_t send_args = {};
    send_args.fd = fds[0];
    thrd_t t;
    int thrd_create_result = thrd_create(&t, send_thread, &send_args);
    ASSERT_EQ(thrd_create_result, thrd_success, "create blocking send thread");

    shutdown(fds[1], SHUT_RD);

    ASSERT_EQ(thrd_join(t, NULL), thrd_success, "join blocking send thread");

    EXPECT_EQ(send_args.send_result, -1, "send should have returned -1");
    EXPECT_EQ(send_args.send_errno, EPIPE, "send should have set errno to EPIPE");

    END_TEST;
}

BEGIN_TEST_CASE(mxio_socketpair_test)
RUN_TEST(socketpair_test);
RUN_TEST(socketpair_shutdown_rd_test);
RUN_TEST(socketpair_shutdown_wr_test);
RUN_TEST(socketpair_shutdown_rdwr_test);
RUN_TEST(socketpair_shutdown_self_wr_poll_test);
RUN_TEST(socketpair_shutdown_peer_wr_poll_test);
RUN_TEST(socketpair_shutdown_self_rd_during_recv_test);
RUN_TEST(socketpair_shutdown_peer_wr_during_recv_test);
RUN_TEST(socketpair_shutdown_self_wr_during_send_test);
RUN_TEST(socketpair_shutdown_peer_rd_during_send_test);
END_TEST_CASE(mxio_socketpair_test)
