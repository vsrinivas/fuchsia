// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <zxtest/zxtest.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

static void create_socket_fdio_pair(zx_handle_t* socket_out, int* fd_out) {
    // Create new socket pair.
    zx_handle_t s1, s2;
    ASSERT_OK(zx_socket_create(ZX_SOCKET_STREAM | ZX_SOCKET_HAS_CONTROL, &s1, &s2), "Socket create failed");

    // We need the FDIO to act like it's connected.
    // ZXSIO_SIGNAL_CONNECTED is private, but we know the value.
    zx_status_t status = zx_object_signal(s2, 0, ZX_USER_SIGNAL_3);
    ASSERT_EQ(status, ZX_OK);

    // Convert one socket to FDIO
    int fd = -1;
    ASSERT_OK(fdio_fd_create(s2, &fd), "Socket from handle failed");

    *fd_out = fd;
    *socket_out = s1;
}

static void set_nonblocking_io(int fd) {
    int flags = fcntl(fd, F_GETFL);
    EXPECT_NE(-1, flags, "fcntl failed");
    EXPECT_NE(-1, fcntl(fd, F_SETFL, flags | O_NONBLOCK), "Set NONBLOCK failed");
}

// Verify scenario, where multi-segment recvmsg is requested, but the socket has
// just enough data to *completely* fill one segment.
// In this scenario, an attempt to read data for the next segment immediately
// fails with ZX_ERR_SHOULD_WAIT, and this may lead to bogus EAGAIN even if some
// data has actually been read.
TEST(SocketTest, RecvmsgNonblockBoundary) {
    zx_handle_t s;
    int fd;

    create_socket_fdio_pair(&s, &fd);
    set_nonblocking_io(fd);

    // Write 4 bytes of data to socket.
    size_t actual;
    const uint32_t data_out = 0x12345678;
    EXPECT_OK(zx_socket_write(s, 0, &data_out, sizeof(data_out), &actual), "Socket write failed");
    EXPECT_EQ(sizeof(data_out), actual, "Socket write length mismatch");

    uint32_t data_in1, data_in2;
    // Fail at compilation stage if anyone changes types.
    // This is mandatory here: we need the first chunk to be exactly the same
    // length as total size of data we just wrote.
    assert(sizeof(data_in1) == sizeof(data_out));

    struct iovec iov[2];
    iov[0].iov_base = &data_in1;
    iov[0].iov_len = sizeof(data_in1);
    iov[1].iov_base = &data_in2;
    iov[1].iov_len = sizeof(data_in2);

    struct msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = sizeof(iov) / sizeof(*iov);
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    actual = recvmsg(fd, &msg, 0);
    EXPECT_EQ(4u, actual);

    zx_handle_close(s);
    close(fd);
}

// Verify scenario, where multi-segment sendmsg is requested, but the socket has
// just enough spare buffer to *completely* read one segment.
// In this scenario, an attempt to send second segment should immediately fail
// with ZX_ERR_SHOULD_WAIT, but the sendmsg should report first segment length
// rather than failing with EAGAIN.
TEST(SocketTest, SendmsgNonblockBoundary) {
    const size_t memlength = 65536;
    void* memchunk = malloc(memlength);

    struct iovec iov[2];
    iov[0].iov_base = memchunk;
    iov[0].iov_len = memlength;
    iov[1].iov_base = memchunk;
    iov[1].iov_len = memlength;

    zx_handle_t s;
    int fd;

    create_socket_fdio_pair(&s, &fd);
    set_nonblocking_io(fd);

    struct msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = sizeof(iov) / sizeof(*iov);
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    // 1. Keep sending data until socket can take no more.
    for (;;) {
        ssize_t count = sendmsg(fd, &msg, 0);
        if (count < 0) {
            if (errno == EAGAIN) {
                break;
            }
        }
        EXPECT_GE(count, 0);
    }

    // 2. Consume one segment of the data
    size_t actual = 0;
    zx_status_t status = zx_socket_read(s, 0, memchunk, memlength, &actual);
    EXPECT_EQ(memlength, actual);
    EXPECT_OK(status);

    // 3. Push again 2 packets of <memlength> bytes, observe only one sent.
    EXPECT_EQ((ssize_t)memlength, sendmsg(fd, &msg, 0));

    zx_handle_close(s);
    close(fd);
    free(memchunk);
}
