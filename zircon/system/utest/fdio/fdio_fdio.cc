// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/unsafe.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <lib/zx/vmo.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/limits.h>

#include <unittest/unittest.h>

static bool null_create_test() {
    BEGIN_TEST;

    fdio_t* io = fdio_null_create();
    fbl::unique_fd fd(fdio_bind_to_fd(io, -1, 0));
    EXPECT_LE(0, fd.get());
    EXPECT_EQ(3, write(fd.get(), "abc", 3));

    END_TEST;
}

static bool create_socket_test() {
    BEGIN_TEST;

    zx::socket s1, s2;
    ASSERT_EQ(ZX_OK, zx::socket::create(0, &s1, &s2));

    fdio_t* io = nullptr;
    ASSERT_EQ(ZX_OK, fdio_create(s1.release(), &io));
    ASSERT_NE(nullptr, io);

    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));

    fbl::unique_fd fd(fdio_bind_to_fd(io, -1, 0));
    EXPECT_LE(0, fd.get());
    EXPECT_EQ(3, write(fd.get(), "abc", 3));
    size_t actual = 0;
    EXPECT_EQ(ZX_OK, s2.read(0, buffer, sizeof(buffer), &actual));
    EXPECT_EQ(3, actual);
    EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>("abc"), buffer, actual, "Readback mismatch");

    END_TEST;
}

static bool create_vmo_test() {
    BEGIN_TEST;

    zx::vmo vmo;
    ASSERT_EQ(ZX_OK, zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));

    fdio_t* io = nullptr;
    ASSERT_EQ(ZX_OK, fdio_create(vmo.release(), &io));
    ASSERT_NE(nullptr, io);

    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));

    fbl::unique_fd fd(fdio_bind_to_fd(io, -1, 0));
    EXPECT_LE(0, fd.get());
    EXPECT_EQ(3, write(fd.get(), "xyz", 3));
    ssize_t actual = pread(fd.get(), buffer, sizeof(buffer), 0);
    EXPECT_EQ(sizeof(buffer), actual);
    EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>("xyz"), buffer, 3, "Readback mismatch");

    END_TEST;
}

static bool bind_to_fd_again_test() {
    BEGIN_TEST;

    zx::socket s1, s2;
    ASSERT_EQ(ZX_OK, zx::socket::create(0, &s1, &s2));

    fdio_t* io = nullptr;
    ASSERT_EQ(ZX_OK, fdio_create(s1.release(), &io));

    fbl::unique_fd fd(fdio_bind_to_fd(io, -1, 0));
    EXPECT_LE(0, fd.get());
    EXPECT_EQ(0, fcntl(fd.get(), F_GETFD));

    fdio_t* io2 = fdio_null_create();
    fbl::unique_fd fd2(fdio_bind_to_fd(io2, fd.get(), -1));
    EXPECT_EQ(fd.get(), fd2.get());
    (void)fd.release();

    zx_signals_t observed = 0;
    ASSERT_EQ(ZX_OK, s2.wait_one(ZX_SOCKET_PEER_CLOSED, zx::time::infinite_past(), &observed));
    ASSERT_TRUE(observed & ZX_SOCKET_PEER_CLOSED);

    END_TEST;
}

static bool find_unused_fd(int starting_fd, int* out_fd) {
    for (int fd = starting_fd; fd < FDIO_MAX_FD; ++fd) {
        if (fcntl(fd, F_GETFD) == -1) {
            *out_fd = fd;
            return true;
        }
    }
    return false;
}

static bool unbind_from_fd_test() {
    BEGIN_TEST;

    int unused_fd = 0;
    ASSERT_TRUE(find_unused_fd(37, &unused_fd));
    ASSERT_EQ(-1, fcntl(unused_fd, F_GETFD));

    fdio_t* io = fdio_null_create();
    fbl::unique_fd fd(fdio_bind_to_fd(io, unused_fd, -1));
    EXPECT_EQ(unused_fd, fd.get());
    EXPECT_EQ(0, fcntl(unused_fd, F_GETFD));
    fdio_t* io2 = nullptr;
    EXPECT_EQ(ZX_OK, fdio_unbind_from_fd(fd.get(), &io2));
    EXPECT_EQ(-1, fcntl(unused_fd, F_GETFD));
    (void)fd.release();
    EXPECT_EQ(io, io2);
    fdio_unsafe_release(io2);

    END_TEST;
}

static bool get_service_handle_test() {
    BEGIN_TEST;

    int unused_fd = 0;
    ASSERT_TRUE(find_unused_fd(37, &unused_fd));
    ASSERT_EQ(-1, fcntl(unused_fd, F_GETFD));

    zx::channel h1;
    EXPECT_EQ(ZX_ERR_NOT_FOUND, fdio_get_service_handle(unused_fd, h1.reset_and_get_address()));
    EXPECT_EQ(ZX_ERR_NOT_FOUND, fdio_get_service_handle(-1, h1.reset_and_get_address()));

    fdio_t* io = fdio_null_create();
    fbl::unique_fd fd(fdio_bind_to_fd(io, -1, 0));
    EXPECT_LE(0, fd.get());
    EXPECT_EQ(0, fcntl(fd.get(), F_GETFD));
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, fdio_get_service_handle(fd.get(), h1.reset_and_get_address()));
    EXPECT_EQ(-1, fcntl(fd.get(), F_GETFD));
    (void)fd.release();

    fd.reset(open("/svc", O_DIRECTORY | O_RDONLY));
    EXPECT_LE(0, fd.get());
    EXPECT_EQ(0, fcntl(fd.get(), F_GETFD));
    EXPECT_EQ(ZX_OK, fdio_get_service_handle(fd.get(), h1.reset_and_get_address()));
    EXPECT_EQ(-1, fcntl(fd.get(), F_GETFD));
    (void)fd.release();

    fd.reset(open("/svc", O_DIRECTORY | O_RDONLY));
    EXPECT_LE(0, fd.get());
    fbl::unique_fd fd2(dup(fd.get()));
    EXPECT_LE(0, fd2.get());
    EXPECT_EQ(0, fcntl(fd.get(), F_GETFD));
    EXPECT_EQ(0, fcntl(fd2.get(), F_GETFD));
    EXPECT_EQ(ZX_ERR_UNAVAILABLE, fdio_get_service_handle(fd.get(), h1.reset_and_get_address()));
    EXPECT_EQ(-1, fcntl(fd.get(), F_GETFD));
    (void)fd.release();
    EXPECT_EQ(0, fcntl(fd2.get(), F_GETFD));
    int raw_fd = fd2.get();
    fd2.reset();
    EXPECT_EQ(-1, fcntl(raw_fd, F_GETFD));

    END_TEST;
}

BEGIN_TEST_CASE(fdio_fdio_test)
RUN_TEST(null_create_test)
RUN_TEST(create_socket_test)
RUN_TEST(create_vmo_test)
RUN_TEST(bind_to_fd_again_test)
RUN_TEST(unbind_from_fd_test)
RUN_TEST(get_service_handle_test)
END_TEST_CASE(fdio_fdio_test)
