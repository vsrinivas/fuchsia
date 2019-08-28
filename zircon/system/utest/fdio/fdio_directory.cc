// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/c/fidl.h>
#include <fuchsia/process/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <zxtest/zxtest.h>

TEST(DirectoryTest, ServiceConnect) {
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, fdio_service_connect(nullptr, ZX_HANDLE_INVALID));

  zx::channel h1, h2;
  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  ASSERT_EQ(ZX_ERR_NOT_FOUND, fdio_service_connect("/x/y/z", h1.release()));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, fdio_service_connect("/", h2.release()));

  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  ASSERT_OK(fdio_service_connect("/svc/" fuchsia_process_Launcher_Name, h1.release()));
}

TEST(DirectoryTest, Open) {
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, fdio_open(nullptr, 0, ZX_HANDLE_INVALID));

  zx::channel h1, h2;
  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  ASSERT_EQ(ZX_ERR_NOT_FOUND, fdio_open("/x/y/z", fuchsia_io_OPEN_RIGHT_READABLE, h1.release()));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, fdio_open("/", fuchsia_io_OPEN_RIGHT_READABLE, h2.release()));

  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  ASSERT_OK(fdio_open("/svc", fuchsia_io_OPEN_RIGHT_READABLE, h1.release()));

  zx::channel h3, h4;
  ASSERT_OK(zx::channel::create(0, &h3, &h4));
  ASSERT_OK(fdio_service_connect_at(h2.get(), fuchsia_process_Launcher_Name, h3.release()));
  ASSERT_OK(fdio_open_at(h2.get(), fuchsia_process_Launcher_Name, fuchsia_io_OPEN_RIGHT_READABLE,
                         h4.release()));

  h3.reset(fdio_service_clone(h2.get()));
  ASSERT_TRUE(h3.is_valid());

  ASSERT_OK(zx::channel::create(0, &h3, &h4));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, fdio_service_clone_to(h2.get(), ZX_HANDLE_INVALID));
  ASSERT_OK(fdio_service_clone_to(h2.get(), h3.release()));
}

std::string new_path(const char* file) {
  const char* root_dir = getenv("TEST_ROOT_DIR");
  if (root_dir == nullptr) {
    root_dir = "";
  }
  return std::string(root_dir) + "/" + file;
}

TEST(DirectoryTest, OpenFD) {
  int fd = -1;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, fdio_open_fd(nullptr, fuchsia_io_OPEN_RIGHT_READABLE, &fd));
  ASSERT_EQ(ZX_ERR_NOT_FOUND, fdio_open_fd("/x/y/z", fuchsia_io_OPEN_RIGHT_READABLE, &fd));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, fdio_open_fd("/", fuchsia_io_OPEN_RIGHT_READABLE, &fd));

  std::string test_sys_path = new_path("test/sys");
  ASSERT_OK(fdio_open_fd(test_sys_path.c_str(), fuchsia_io_OPEN_RIGHT_READABLE, &fd));
  ASSERT_TRUE(fd >= 0);

  int fd2 = -1;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, fdio_open_fd_at(fd, nullptr, fuchsia_io_OPEN_RIGHT_READABLE,
                                                 &fd2));
  ASSERT_EQ(fd2, -1);
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, fdio_open_fd_at(fd, "some-nonexistent-file",
                                                fuchsia_io_OPEN_RIGHT_READABLE, &fd2));
  ASSERT_EQ(fd2, -1);

  // We expect the binary that this file is compiled into to exist
  ASSERT_OK(fdio_open_fd_at(fd, "fdio-test", fuchsia_io_OPEN_RIGHT_READABLE, &fd2));
  ASSERT_TRUE(fd >= 0);

  // Verify that we can actually read from that file, since opens are async.
  char buf[256];
  ssize_t bytes_read = read(fd2, buf, 256);
  ASSERT_EQ(bytes_read, 256);
}
