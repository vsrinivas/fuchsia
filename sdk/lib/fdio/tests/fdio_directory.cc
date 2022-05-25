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

const fuchsia_io::wire::OpenFlags kReadFlags = fuchsia_io::wire::OpenFlags::kRightReadable;

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
  ASSERT_STATUS(ZX_ERR_NOT_FOUND,
                fdio_open("/x/y/z", static_cast<uint32_t>(kReadFlags), h1.release()));
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED,
                fdio_open("/", static_cast<uint32_t>(kReadFlags), h2.release()));

  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  ASSERT_OK(fdio_open("/svc", static_cast<uint32_t>(kReadFlags), h1.release()));

  zx::channel h3, h4;
  ASSERT_OK(zx::channel::create(0, &h3, &h4));
  ASSERT_OK(fdio_service_connect_at(
      h2.get(), fidl::DiscoverableProtocolName<fuchsia_process::Launcher>, h3.release()));
  ASSERT_OK(fdio_open_at(h2.get(), fidl::DiscoverableProtocolName<fuchsia_process::Launcher>,
                         static_cast<uint32_t>(kReadFlags), h4.release()));

  h3.reset(fdio_service_clone(h2.get()));
  ASSERT_TRUE(h3.is_valid());

  ASSERT_OK(zx::channel::create(0, &h3, &h4));
  ASSERT_STATUS(ZX_ERR_INVALID_ARGS, fdio_service_clone_to(h2.get(), ZX_HANDLE_INVALID));
  ASSERT_OK(fdio_service_clone_to(h2.get(), h3.release()));
}

TEST(DirectoryTest, OpenFD) {
  // kInvalidLengthPath is `/a{4095}\0` (leading forward slash then 4,095 'a's then null),
  // which is 4,097 bytes (including the null) which is one longer than the maximum allowed path
  // https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.io/io.fidl;l=47;drc=e7cbd843e8ced20ea21f9213989d803ae64fcfaf
  constexpr std::string_view kInvalidLengthPath =
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
  EXPECT_EQ(kInvalidLengthPath.length(), 4096);
  {
    fbl::unique_fd fd;
    ASSERT_STATUS(ZX_ERR_INVALID_ARGS, fdio_open_fd(nullptr, static_cast<uint32_t>(kReadFlags),
                                                    fd.reset_and_get_address()));
    ASSERT_STATUS(ZX_ERR_NOT_FOUND, fdio_open_fd("/x/y/z", static_cast<uint32_t>(kReadFlags),
                                                 fd.reset_and_get_address()));

    // Opening local directories, like the root of the namespace, should be supported.
    ASSERT_OK(fdio_open_fd("/", static_cast<uint32_t>(kReadFlags), fd.reset_and_get_address()));

    // fdio_open_fd canonicalizes the path, per
    // https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.io/io.fidl;l=41-43;drc=e7cbd843e8ced20ea21f9213989d803ae64fcfaf
    ASSERT_OK(
        fdio_open_fd("/pkg/..", static_cast<uint32_t>(kReadFlags), fd.reset_and_get_address()));

    // fdio_open_fd rejects paths of 4,097 bytes (including the null) or more.
    ASSERT_STATUS(ZX_ERR_BAD_PATH,
                  fdio_open_fd(kInvalidLengthPath.data(), static_cast<uint32_t>(kReadFlags),
                               fd.reset_and_get_address()));

    // fdio_open_fd's path canonicalization of consecutive '/'s works with fdio_open_fd's
    // requirement for a leading slash.
    ASSERT_OK(fdio_open_fd("//", static_cast<uint32_t>(kReadFlags), fd.reset_and_get_address()));

    // Relative paths are interpreted to CWD which is '/' at this point of the test.
    ASSERT_OK(fdio_open_fd("pkg", static_cast<uint32_t>(kReadFlags), fd.reset_and_get_address()));

    // fdio_open_fd sets OPEN_FLAG_DIRECTORY if the path ends in '/'.
    ASSERT_STATUS(ZX_ERR_NOT_DIR,
                  fdio_open_fd("/pkg/test/fdio-test/", static_cast<uint32_t>(kReadFlags),
                               fd.reset_and_get_address()));
  }

  {
    ASSERT_EQ(chdir("/pkg"), 0, "errno %d: %s", errno, strerror(errno));

    fbl::unique_fd fd;
    ASSERT_OK(fdio_open_fd("test", static_cast<uint32_t>(kReadFlags), fd.reset_and_get_address()));
    ASSERT_TRUE(fd.is_valid());

    ASSERT_EQ(chdir("/"), 0, "errno %d: %s", errno, strerror(errno));
  }

  {
    fbl::unique_fd fd;
    ASSERT_OK(
        fdio_open_fd("/pkg/test", static_cast<uint32_t>(kReadFlags), fd.reset_and_get_address()));
    ASSERT_TRUE(fd.is_valid());

    fbl::unique_fd fd2;
    ASSERT_STATUS(ZX_ERR_INVALID_ARGS,
                  fdio_open_fd_at(fd.get(), nullptr, static_cast<uint32_t>(kReadFlags),
                                  fd2.reset_and_get_address()));
    ASSERT_FALSE(fd2.is_valid());
    ASSERT_STATUS(ZX_ERR_NOT_FOUND,
                  fdio_open_fd_at(fd.get(), "some-nonexistent-file",
                                  static_cast<uint32_t>(kReadFlags), fd2.reset_and_get_address()));
    ASSERT_FALSE(fd2.is_valid());

    // fdio_open_fd_at() should not resolve absolute paths to the root directory, unlike openat().
    ASSERT_STATUS(ZX_ERR_INVALID_ARGS,
                  fdio_open_fd_at(fd.get(), "/pkg", static_cast<uint32_t>(kReadFlags),
                                  fd2.reset_and_get_address()));
    ASSERT_FALSE(fd2.is_valid());

    // fdio_open_fd_at() also should not interpret absolute paths as relative paths to the provided
    // fd.
    ASSERT_STATUS(ZX_ERR_INVALID_ARGS,
                  fdio_open_fd_at(fd.get(), "/fdio-test", static_cast<uint32_t>(kReadFlags),
                                  fd2.reset_and_get_address()));
    ASSERT_FALSE(fd2.is_valid());

    // fdio_open_fd_at rejects paths of 4,097 bytes (including the null) or more.
    ASSERT_STATUS(ZX_ERR_BAD_PATH,
                  fdio_open_fd_at(fd.get(), kInvalidLengthPath.data(),
                                  static_cast<uint32_t>(kReadFlags), fd2.reset_and_get_address()));

    // We expect the binary that this file is compiled into to exist
    ASSERT_OK(fdio_open_fd_at(fd.get(), "fdio-test", static_cast<uint32_t>(kReadFlags),
                              fd2.reset_and_get_address()));
    ASSERT_TRUE(fd2.is_valid());

    // Verify that we can actually read from that file.
    char buf[256];
    ssize_t bytes_read = read(fd2.get(), buf, 256);
    ASSERT_EQ(bytes_read, 256);

    // fdio_open_fd_at canonicalizes the path, per
    // https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.io/io.fidl;l=41-43;drc=e7cbd843e8ced20ea21f9213989d803ae64fcfaf
    ASSERT_OK(fdio_open_fd_at(fd.get(), "fdio-test/..", static_cast<uint32_t>(kReadFlags),
                              fd2.reset_and_get_address()));
    ASSERT_TRUE(fd2.is_valid());

    // fdio_open_fd_at sets OPEN_FLAG_DIRECTORY if the path ends in '/'.
    ASSERT_STATUS(ZX_ERR_NOT_DIR,
                  fdio_open_fd_at(fd.get(), "fdio-test/", static_cast<uint32_t>(kReadFlags),
                                  fd2.reset_and_get_address()));
    ASSERT_FALSE(fd2.is_valid());
  }
}

}  // namespace
