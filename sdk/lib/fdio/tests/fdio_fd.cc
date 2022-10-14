// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/socket.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

TEST(FileDescriptorTest, CreateSocket) {
  zx::socket h1, h2;
  ASSERT_OK(zx::socket::create(0, &h1, &h2));

  int fd = -1;
  ASSERT_OK(fdio_fd_create(h1.release(), &fd));
  ASSERT_LE(0, fd);

  const char* message = "hello, my old friend.";
  ssize_t length = strlen(message);
  ASSERT_EQ(length, write(fd, message, length));
  close(fd);
}

TEST(FileDescriptorTest, CreateVMO) {
  const size_t kSize = 4096;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kSize, 0, &vmo));

  const char* message = "hello, vmo.";
  ASSERT_OK(vmo.write(message, 0, strlen(message)));

  int fd = -1;
  ASSERT_OK(fdio_fd_create(vmo.release(), &fd));
  ASSERT_LE(0, fd);

  struct stat info = {};
  ASSERT_EQ(0, fstat(fd, &info));
  EXPECT_EQ(4096, info.st_size);

  char buffer[1024];
  memset(buffer, 0, sizeof(buffer));
  ASSERT_EQ(read(fd, buffer, sizeof(buffer)), ssize_t(sizeof(buffer)));
  ASSERT_EQ(7, lseek(fd, 7, SEEK_SET));
  EXPECT_EQ(0, strcmp(message, buffer));

  const char* updated = "fd.";
  ASSERT_EQ(4, write(fd, updated, strlen(updated) + 1));

  memset(buffer, 0, sizeof(buffer));
  ASSERT_EQ(pread(fd, buffer, sizeof(buffer), 0u), ssize_t(sizeof(buffer)));
  EXPECT_EQ(0, strcmp("hello, fd.", buffer));

  ASSERT_EQ(1, pwrite(fd, "!", 1, 9));
  memset(buffer, 0, sizeof(buffer));
  ASSERT_EQ(pread(fd, buffer, sizeof(buffer), 0u), ssize_t(sizeof(buffer)));
  EXPECT_EQ(0, strcmp("hello, fd!", buffer));

  ASSERT_EQ(4096, lseek(fd, 4096, SEEK_SET));
  memset(buffer, 0, sizeof(buffer));
  ASSERT_EQ(0, read(fd, buffer, sizeof(buffer)));

  close(fd);
}

TEST(FileDescriptorTest, CWDClone) {
  zx_handle_t dir = ZX_HANDLE_INVALID;
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, fdio_cwd_clone(&dir));
  ASSERT_EQ(ZX_HANDLE_INVALID, dir);
}

TEST(FileDescriptorTest, CloneSocket) {
  zx::socket h1, h2;
  ASSERT_OK(zx::socket::create(0, &h1, &h2));

  int fd = -1;
  ASSERT_OK(fdio_fd_create(h1.release(), &fd));
  ASSERT_LE(0, fd);

  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_OK(fdio_fd_clone(fd, &handle));
  ASSERT_NE(ZX_HANDLE_INVALID, handle);

  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  ASSERT_OK(status);
  ASSERT_EQ(ZX_OBJ_TYPE_SOCKET, info.type);
  zx_handle_close(handle);

  int fd2 = dup(fd);
  ASSERT_LE(0, fd2);

  ASSERT_OK(fdio_fd_clone(fd, &handle));
  zx_handle_close(handle);

  ASSERT_EQ(0, close(fd));
  ASSERT_EQ(0, close(fd2));
}

TEST(FileDescriptorTest, CloneVMO) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(4096, 0, &vmo));

  int fd = -1;
  ASSERT_OK(fdio_fd_create(vmo.release(), &fd));
  ASSERT_LE(0, fd);

  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_OK(fdio_fd_clone(fd, &handle));
  ASSERT_NE(ZX_HANDLE_INVALID, handle);

  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  ASSERT_OK(status);
  ASSERT_EQ(ZX_OBJ_TYPE_VMO, info.type);
  zx_handle_close(handle);

  ASSERT_EQ(0, close(fd));
}

TEST(FileDescriptorTest, CloneAndTransferSocketPair) {
  std::array<fbl::unique_fd, 2> fds;
  int int_fds[fds.size()];
  ASSERT_SUCCESS(socketpair(AF_UNIX, SOCK_STREAM, 0, int_fds));
  for (size_t i = 0; i < fds.size(); ++i) {
    fds[i].reset(int_fds[i]);
  }

  zx::handle handle;
  ASSERT_OK(fdio_fd_clone(fds[0].get(), handle.reset_and_get_address()));

  fbl::unique_fd cloned_fd;
  ASSERT_OK(fdio_fd_create(handle.release(), cloned_fd.reset_and_get_address()));

  ASSERT_OK(fdio_fd_transfer(fds[0].release(), handle.reset_and_get_address()));

  fbl::unique_fd transferred_fd;
  ASSERT_OK(fdio_fd_create(handle.release(), transferred_fd.reset_and_get_address()));

  // Verify that an operation specific to socketpairs works on these fds.
  ASSERT_SUCCESS(shutdown(transferred_fd.get(), SHUT_WR));
  ASSERT_SUCCESS(shutdown(cloned_fd.get(), SHUT_RD));
}

TEST(FileDescriptorTest, TransferSocket) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, fdio_fd_transfer(151465, &handle));

  zx::socket h1, h2;
  ASSERT_OK(zx::socket::create(0, &h1, &h2));

  int fd = -1;
  ASSERT_OK(fdio_fd_create(h1.release(), &fd));
  ASSERT_LE(0, fd);

  ASSERT_OK(fdio_fd_transfer(fd, &handle));
  ASSERT_NE(ZX_HANDLE_INVALID, handle);

  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  ASSERT_OK(status);
  ASSERT_EQ(ZX_OBJ_TYPE_SOCKET, info.type);
  zx_handle_close(handle);
  ASSERT_EQ(-1, close(fd));
}

TEST(FileDescriptorTest, TransferVMO) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(4096, 0, &vmo));

  int fd = -1;
  ASSERT_OK(fdio_fd_create(vmo.release(), &fd));
  ASSERT_LE(0, fd);

  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_OK(fdio_fd_transfer(fd, &handle));
  ASSERT_NE(ZX_HANDLE_INVALID, handle);

  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  ASSERT_OK(status);
  ASSERT_EQ(ZX_OBJ_TYPE_VMO, info.type);
  zx_handle_close(handle);
  ASSERT_EQ(-1, close(fd));
}

TEST(FileDescriptorTest, TransferAfterDup) {
  zx::socket h1, h2;
  ASSERT_OK(zx::socket::create(0, &h1, &h2));

  int fd = -1;
  ASSERT_OK(fdio_fd_create(h1.release(), &fd));
  ASSERT_LE(0, fd);

  int fd2 = dup(fd);
  ASSERT_LE(0, fd2);

  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_EQ(ZX_ERR_UNAVAILABLE, fdio_fd_transfer(fd, &handle));
  ASSERT_EQ(ZX_HANDLE_INVALID, handle);

  // fdio_fd_transfer always consumes |fd|.
  EXPECT_EQ(close(fd), -1);
  EXPECT_ERRNO(EBADF);
  EXPECT_EQ(close(fd2), 0);
}

TEST(FileDescriptorTest, TransferOrCloneError) {
  zx::handle handle;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, fdio_fd_transfer_or_clone(151465, handle.reset_and_get_address()));
}

TEST(FileDescriptorTest, TransferOrCloneTransfers) {
  zx::socket h1, h2;
  ASSERT_OK(zx::socket::create(0, &h1, &h2));

  int fd = -1;
  const zx_handle_t original_handle = h1.get();
  ASSERT_OK(fdio_fd_create(h1.release(), &fd));
  ASSERT_LE(0, fd);

  zx::handle handle;
  ASSERT_OK(fdio_fd_transfer_or_clone(fd, handle.reset_and_get_address()));

  // Handle should be the same as the one that the FD was created with.
  EXPECT_EQ(original_handle, handle.get());

  // fdio_fd_transfer_or_clone() always consumes |fd|.
  EXPECT_EQ(close(fd), -1);
  EXPECT_ERRNO(EBADF);
}

TEST(FileDescriptorTest, TransferOrCloneAfterDup) {
  zx::socket h1, h2;
  ASSERT_OK(zx::socket::create(0, &h1, &h2));

  int fd = -1;
  const zx_handle_t original_handle = h1.get();
  ASSERT_OK(fdio_fd_create(h1.release(), &fd));
  ASSERT_LE(0, fd);

  int fd2 = dup(fd);
  ASSERT_LE(0, fd2);

  zx::handle handle;
  ASSERT_OK(fdio_fd_transfer_or_clone(fd, handle.reset_and_get_address()));
  ASSERT_NE(ZX_HANDLE_INVALID, handle);

  // Duplicated FDs must be cloned, so the handle should differ from the one
  // used to create the original FD.
  EXPECT_NE(original_handle, handle);

  // fdio_fd_transfer_or_clone() always consumes |fd|.
  EXPECT_EQ(close(fd), -1);
  EXPECT_ERRNO(EBADF);
  EXPECT_EQ(close(fd2), 0);
}

TEST(FileDescriptorTest, DupToSameFdSucceeds) {
  zx::socket h1, h2;
  ASSERT_OK(zx::socket::create(0, &h1, &h2));

  int fd = -1;
  ASSERT_OK(fdio_fd_create(h1.release(), &fd));
  ASSERT_LE(0, fd);

  ASSERT_EQ(fd, dup2(fd, fd));

  const char* message = "hello, my old friend.";
  ssize_t length = strlen(message);
  ASSERT_EQ(length, write(fd, message, length));
  close(fd);
}
