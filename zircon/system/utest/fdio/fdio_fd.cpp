// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fd.h>
#include <lib/zx/socket.h>
#include <string.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <unittest/unittest.h>

bool create_test(void) {
    BEGIN_TEST;

    zx::socket h1, h2;
    ASSERT_EQ(ZX_OK, zx::socket::create(0, &h1, &h2), "");

    int fd = -1;
    ASSERT_EQ(ZX_OK, fdio_fd_create(h1.release(), &fd), "");
    ASSERT_LE(1, fd, "");

    const char* message = "hello, my old friend.";
    ssize_t length = strlen(message);
    ASSERT_EQ(length, write(fd, message, length), "");
    close(fd);

    END_TEST;
}

bool cwd_clone_test(void) {
    BEGIN_TEST;

    zx_handle_t dir = ZX_HANDLE_INVALID;
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, fdio_cwd_clone(&dir), "");
    ASSERT_EQ(ZX_HANDLE_INVALID, dir, "");

    END_TEST;
}

bool fd_clone_test(void) {
    BEGIN_TEST;

    zx::socket h1, h2;
    ASSERT_EQ(ZX_OK, zx::socket::create(0, &h1, &h2), "");

    int fd = -1;
    ASSERT_EQ(ZX_OK, fdio_fd_create(h1.release(), &fd), "");
    ASSERT_LE(1, fd, "");

    zx_handle_t handle = ZX_HANDLE_INVALID;
    ASSERT_EQ(ZX_OK, fdio_fd_clone(fd, &handle), "");
    ASSERT_NE(ZX_HANDLE_INVALID, handle, "");

    zx_info_handle_basic_t info;
    zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                            sizeof(info), NULL, NULL);
    ASSERT_EQ(ZX_OK, status, "");
    ASSERT_EQ(ZX_OBJ_TYPE_SOCKET, info.type, "");
    zx_handle_close(handle);

    int fd2 = dup(fd);
    ASSERT_LE(1, fd2, "");

    ASSERT_EQ(ZX_OK, fdio_fd_clone(fd, &handle), "");
    zx_handle_close(handle);

    ASSERT_EQ(0, close(fd), "");
    ASSERT_EQ(0, close(fd2), "");

    END_TEST;
}

bool fd_transfer_test(void) {
    BEGIN_TEST;

    zx_handle_t handle = ZX_HANDLE_INVALID;
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, fdio_fd_transfer(151465, &handle), "");

    zx::socket h1, h2;
    ASSERT_EQ(ZX_OK, zx::socket::create(0, &h1, &h2), "");

    int fd = -1;
    ASSERT_EQ(ZX_OK, fdio_fd_create(h1.release(), &fd), "");
    ASSERT_LE(1, fd, "");

    ASSERT_EQ(ZX_OK, fdio_fd_transfer(fd, &handle), "");
    ASSERT_NE(ZX_HANDLE_INVALID, handle, "");

    zx_info_handle_basic_t info;
    zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                            sizeof(info), NULL, NULL);
    ASSERT_EQ(ZX_OK, status, "");
    ASSERT_EQ(ZX_OBJ_TYPE_SOCKET, info.type, "");
    zx_handle_close(handle);
    ASSERT_EQ(-1, close(fd), "");

    END_TEST;
}

bool fd_transfer_after_dup_test(void) {
    BEGIN_TEST;

    zx::socket h1, h2;
    ASSERT_EQ(ZX_OK, zx::socket::create(0, &h1, &h2), "");

    int fd = -1;
    ASSERT_EQ(ZX_OK, fdio_fd_create(h1.release(), &fd), "");
    ASSERT_LE(1, fd, "");

    int fd2 = dup(fd);
    ASSERT_LE(1, fd2, "");

    zx_handle_t handle = ZX_HANDLE_INVALID;
    ASSERT_EQ(ZX_ERR_UNAVAILABLE, fdio_fd_transfer(fd, &handle), "");
    ASSERT_EQ(ZX_HANDLE_INVALID, handle, "");

    // Currently, fdio_fd_transfer does not consume |fd| when it returns
    // ZX_ERR_UNAVAILABLE, but we might want to change that in the future.
    ASSERT_EQ(0, close(fd), "");
    ASSERT_EQ(0, close(fd2), "");

    END_TEST;
}

BEGIN_TEST_CASE(fdio_fd_test)
RUN_TEST(create_test)
RUN_TEST(cwd_clone_test)
RUN_TEST(fd_clone_test)
RUN_TEST(fd_transfer_test)
RUN_TEST(fd_transfer_after_dup_test)
END_TEST_CASE(fdio_fd_test)
