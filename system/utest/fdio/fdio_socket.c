// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <assert.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <lib/fdio/util.h>
#include <unittest/unittest.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

static bool create_socket_fdio_pair(zx_handle_t* socket_out, int* fd_out) {
    // Create new socket pair.
    zx_handle_t s1, s2;
    ASSERT_EQ(ZX_OK, zx_socket_create(ZX_SOCKET_STREAM, &s1, &s2), "Socket create failed");

    // Convert one socket to FDIO
    uint32_t type = PA_FDIO_SOCKET;
    int fd;
    ASSERT_EQ(ZX_OK, fdio_create_fd(&s2, &type, 1, &fd), "Socket from handle failed");

    *fd_out = fd;
    *socket_out = s1;

    return true;
}

static bool set_nonblocking_io(int fd) {
    int flags = fcntl(fd, F_GETFL);
    EXPECT_NE(-1, flags, "fcntl failed");
    EXPECT_NE(-1, fcntl(fd, F_SETFL, flags | O_NONBLOCK), "Set NONBLOCK failed");
    return true;
}

// Verify scenario, where multi-segment recvmsg is requested, but the socket has
// just enough data to *completely* fill one segment.
// In this scenario, an attempt to read data for the next segment immediately
// fails with ZX_ERR_SHOULD_WAIT, and this may lead to bogus EAGAIN even if some
// data has actually been read.
bool socket_recvmsg_nonblock_boundary_test(void) {
    BEGIN_TEST;

    zx_handle_t s;
    int fd;

    if (!create_socket_fdio_pair(&s, &fd) || !set_nonblocking_io(fd)) {
        return false;
    }

    // Write 4 bytes of data to socket.
    size_t actual;
    const uint32_t data_out = 0x12345678;
    EXPECT_EQ(ZX_OK, zx_socket_write(s, 0, &data_out, sizeof(data_out), &actual), "Socket write failed");
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
    EXPECT_EQ(4u, actual, "Socket read failed");

    zx_handle_close(s);
    close(fd);
    END_TEST;
}

BEGIN_TEST_CASE(newsocket_tests)
RUN_TEST(socket_recvmsg_nonblock_boundary_test)
END_TEST_CASE(newsocket_tests)
