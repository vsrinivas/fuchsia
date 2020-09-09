// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/process/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

namespace fuchsia_io = ::llcpp::fuchsia::io;

const uint32_t kReadFlags = fuchsia_io::OPEN_RIGHT_READABLE;

TEST(DirectoryTest, ServiceConnect) {
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, fdio_service_connect(nullptr, ZX_HANDLE_INVALID));

  zx::channel h1, h2;
  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  ASSERT_EQ(ZX_ERR_NOT_FOUND, fdio_service_connect("/x/y/z", h1.release()));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, fdio_service_connect("/", h2.release()));

  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  std::string path = std::string("/svc/") + ::llcpp::fuchsia::process::Launcher::Name;
  ASSERT_OK(fdio_service_connect(path.c_str(), h1.release()));
}

TEST(DirectoryTest, Open) {
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, fdio_open(nullptr, 0, ZX_HANDLE_INVALID));

  zx::channel h1, h2;
  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  ASSERT_EQ(ZX_ERR_NOT_FOUND, fdio_open("/x/y/z", kReadFlags, h1.release()));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, fdio_open("/", kReadFlags, h2.release()));

  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  ASSERT_OK(fdio_open("/svc", kReadFlags, h1.release()));

  zx::channel h3, h4;
  ASSERT_OK(zx::channel::create(0, &h3, &h4));
  ASSERT_OK(
      fdio_service_connect_at(h2.get(), ::llcpp::fuchsia::process::Launcher::Name, h3.release()));
  ASSERT_OK(
      fdio_open_at(h2.get(), ::llcpp::fuchsia::process::Launcher::Name, kReadFlags, h4.release()));

  h3.reset(fdio_service_clone(h2.get()));
  ASSERT_TRUE(h3.is_valid());

  ASSERT_OK(zx::channel::create(0, &h3, &h4));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, fdio_service_clone_to(h2.get(), ZX_HANDLE_INVALID));
  ASSERT_OK(fdio_service_clone_to(h2.get(), h3.release()));
}

TEST(DirectoryTest, OpenFD) {
  {
    fbl::unique_fd fd;
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, fdio_open_fd(nullptr, kReadFlags, fd.reset_and_get_address()));
    ASSERT_EQ(ZX_ERR_NOT_FOUND, fdio_open_fd("/x/y/z", kReadFlags, fd.reset_and_get_address()));

    // Opening local directories, like the root of the namespace, should be supported.
    ASSERT_OK(fdio_open_fd("/", kReadFlags, fd.reset_and_get_address()));
    // But empty path segments don't need to be supported.
    ASSERT_EQ(ZX_ERR_BAD_PATH, fdio_open_fd("//", kReadFlags, fd.reset_and_get_address()));
  }

  {
    fbl::unique_fd fd;
    ASSERT_OK(fdio_open_fd("/pkg/test", kReadFlags, fd.reset_and_get_address()));
    ASSERT_TRUE(fd.is_valid());

    fbl::unique_fd fd2;
    ASSERT_EQ(ZX_ERR_INVALID_ARGS,
              fdio_open_fd_at(fd.get(), nullptr, kReadFlags, fd2.reset_and_get_address()));
    ASSERT_FALSE(fd2.is_valid());
    ASSERT_EQ(ZX_ERR_NOT_FOUND, fdio_open_fd_at(fd.get(), "some-nonexistent-file", kReadFlags,
                                                fd2.reset_and_get_address()));
    ASSERT_FALSE(fd2.is_valid());

    // We expect the binary that this file is compiled into to exist
    ASSERT_OK(fdio_open_fd_at(fd.get(), "fdio-test", kReadFlags, fd2.reset_and_get_address()));
    ASSERT_TRUE(fd2.is_valid());

    // Verify that we can actually read from that file.
    char buf[256];
    ssize_t bytes_read = read(fd2.get(), buf, 256);
    ASSERT_EQ(bytes_read, 256);
  }
}

}  // namespace
