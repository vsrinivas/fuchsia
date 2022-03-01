// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.process/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

namespace fuchsia_io = fuchsia_io;

const uint32_t kReadFlags = fuchsia_io::wire::kOpenRightReadable;

TEST(DirectoryTest, ServiceConnect) {
  ASSERT_STATUS(ZX_ERR_INVALID_ARGS, fdio_service_connect(nullptr, ZX_HANDLE_INVALID));

  zx::channel h1, h2;
  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  ASSERT_STATUS(ZX_ERR_NOT_FOUND, fdio_service_connect("/x/y/z", h1.release()));
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, fdio_service_connect("/", h2.release()));

  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  ASSERT_OK(fdio_service_connect(fidl::DiscoverableProtocolDefaultPath<fuchsia_process::Launcher>,
                                 h1.release()));
}

TEST(DirectoryTest, Open) {
  ASSERT_STATUS(ZX_ERR_INVALID_ARGS, fdio_open(nullptr, 0, ZX_HANDLE_INVALID));

  zx::channel h1, h2;
  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  ASSERT_STATUS(ZX_ERR_NOT_FOUND, fdio_open("/x/y/z", kReadFlags, h1.release()));
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, fdio_open("/", kReadFlags, h2.release()));

  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  ASSERT_OK(fdio_open("/svc", kReadFlags, h1.release()));

  zx::channel h3, h4;
  ASSERT_OK(zx::channel::create(0, &h3, &h4));
  ASSERT_OK(fdio_service_connect_at(
      h2.get(), fidl::DiscoverableProtocolName<fuchsia_process::Launcher>, h3.release()));
  ASSERT_OK(fdio_open_at(h2.get(), fidl::DiscoverableProtocolName<fuchsia_process::Launcher>,
                         kReadFlags, h4.release()));

  h3.reset(fdio_service_clone(h2.get()));
  ASSERT_TRUE(h3.is_valid());

  ASSERT_OK(zx::channel::create(0, &h3, &h4));
  ASSERT_STATUS(ZX_ERR_INVALID_ARGS, fdio_service_clone_to(h2.get(), ZX_HANDLE_INVALID));
  ASSERT_OK(fdio_service_clone_to(h2.get(), h3.release()));
}

TEST(DirectoryTest, OpenFD) {
  // INVALID_LENGTH_PATH is `/a{4095}\0` (leading forward slash then 4,095 'a's then null),
  // which is 4,097 bytes (including the null) which is one longer than the maximum allowed path
  // https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.io/io.fidl;l=47;drc=e7cbd843e8ced20ea21f9213989d803ae64fcfaf
  const auto INVALID_LENGTH_PATH =
      "/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  {
    fbl::unique_fd fd;
    ASSERT_STATUS(ZX_ERR_INVALID_ARGS,
                  fdio_open_fd(nullptr, kReadFlags, fd.reset_and_get_address()));
    ASSERT_STATUS(ZX_ERR_NOT_FOUND, fdio_open_fd("/x/y/z", kReadFlags, fd.reset_and_get_address()));

    // Opening local directories, like the root of the namespace, should be supported.
    ASSERT_OK(fdio_open_fd("/", kReadFlags, fd.reset_and_get_address()));

    // fdio_open_fd canonicalizes the path, per
    // https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.io/io.fidl;l=41-43;drc=e7cbd843e8ced20ea21f9213989d803ae64fcfaf
    ASSERT_OK(fdio_open_fd("/pkg/..", kReadFlags, fd.reset_and_get_address()));

    // fdio_open_fd rejects paths of 4,097 bytes (including the null) or more.
    ASSERT_STATUS(ZX_ERR_BAD_PATH,
                  fdio_open_fd(INVALID_LENGTH_PATH, kReadFlags, fd.reset_and_get_address()));

    // fdio_open_fd's path canonicalization of consecutive '/'s works with fdio_open_fd's
    // requirement for a leading slash.
    ASSERT_OK(fdio_open_fd("//", kReadFlags, fd.reset_and_get_address()));

    // Path must start with '/'.
    ASSERT_STATUS(ZX_ERR_NOT_FOUND, fdio_open_fd("pkg", kReadFlags, fd.reset_and_get_address()));

    // fdio_open_fd sets ZX_FS_FLAG_DIRECTORY if the path ends in '/'.
    ASSERT_STATUS(ZX_ERR_NOT_DIR,
                  fdio_open_fd("/pkg/test/fdio-test/", kReadFlags, fd.reset_and_get_address()));
  }

  {
    fbl::unique_fd fd;
    ASSERT_OK(fdio_open_fd("/pkg/test", kReadFlags, fd.reset_and_get_address()));
    ASSERT_TRUE(fd.is_valid());

    fbl::unique_fd fd2;
    ASSERT_STATUS(ZX_ERR_INVALID_ARGS,
                  fdio_open_fd_at(fd.get(), nullptr, kReadFlags, fd2.reset_and_get_address()));
    ASSERT_FALSE(fd2.is_valid());
    ASSERT_STATUS(ZX_ERR_NOT_FOUND, fdio_open_fd_at(fd.get(), "some-nonexistent-file", kReadFlags,
                                                    fd2.reset_and_get_address()));
    ASSERT_FALSE(fd2.is_valid());

    // fdio_open_fd_at rejects paths of 4,097 bytes (including the null) or more.
    ASSERT_STATUS(ZX_ERR_BAD_PATH, fdio_open_fd_at(fd.get(), INVALID_LENGTH_PATH, kReadFlags,
                                                   fd2.reset_and_get_address()));

    // We expect the binary that this file is compiled into to exist
    ASSERT_OK(fdio_open_fd_at(fd.get(), "fdio-test", kReadFlags, fd2.reset_and_get_address()));
    ASSERT_TRUE(fd2.is_valid());

    // Verify that we can actually read from that file.
    char buf[256];
    ssize_t bytes_read = read(fd2.get(), buf, 256);
    ASSERT_EQ(bytes_read, 256);

    // fdio_open_fd_at canonicalizes the path, per
    // https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.io/io.fidl;l=41-43;drc=e7cbd843e8ced20ea21f9213989d803ae64fcfaf
    ASSERT_OK(fdio_open_fd_at(fd.get(), "fdio-test/..", kReadFlags, fd2.reset_and_get_address()));
    ASSERT_TRUE(fd2.is_valid());

    // fdio_open_fd_at sets ZX_FS_FLAG_DIRECTORY if the path ends in '/'.
    ASSERT_STATUS(ZX_ERR_NOT_DIR,
                  fdio_open_fd_at(fd.get(), "fdio-test/", kReadFlags, fd2.reset_and_get_address()));
    ASSERT_FALSE(fd2.is_valid());
  }
}

}  // namespace
