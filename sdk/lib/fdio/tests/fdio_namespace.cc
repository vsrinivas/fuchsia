// Copyright 2022 The Fuchsia Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/namespace.h>
#include <sys/mman.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

TEST(NamespaceTest, CreateDestroy) {
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));
  ASSERT_OK(fdio_ns_destroy(ns));
}

// Test that namespace functions properly handle null path pointers.
TEST(NamespaceTest, NullPaths) {
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));

  zx::channel ch0, ch1;
  ASSERT_OK(zx::channel::create(0, &ch0, &ch1));

  EXPECT_STATUS(fdio_ns_bind(ns, nullptr, ch0.release()), ZX_ERR_INVALID_ARGS);

  EXPECT_STATUS(fdio_ns_unbind(ns, nullptr), ZX_ERR_INVALID_ARGS);

  EXPECT_FALSE(fdio_ns_is_bound(ns, nullptr));

  fbl::unique_fd fd(memfd_create("TestFd", 0));
  ASSERT_GE(fd.get(), 0);
  EXPECT_STATUS(fdio_ns_bind_fd(ns, nullptr, fd.get()), ZX_ERR_INVALID_ARGS);

  zx::channel service0, service1;
  ASSERT_OK(zx::channel::create(0, &service0, &service1));
  EXPECT_STATUS(fdio_ns_open(ns, nullptr, 0, service0.release()), ZX_ERR_INVALID_ARGS);

  ASSERT_OK(fdio_ns_destroy(ns));
}

TEST(NamespaceTest, BindUnbindCanonicalPaths) {
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));

  zx::channel ch0, ch1;
  ASSERT_OK(zx::channel::create(0, &ch0, &ch1));

  ASSERT_OK(fdio_ns_bind(ns, "/foo", ch0.release()));

  ASSERT_OK(fdio_ns_unbind(ns, "/foo"));

  ASSERT_OK(fdio_ns_destroy(ns));
}

TEST(NamespaceTest, BindUnbindNonCanonical) {
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));

  zx::channel ch0, ch1;
  ASSERT_OK(zx::channel::create(0, &ch0, &ch1));

  // These non-canonical paths both canonicalize to "/foo".
  ASSERT_OK(fdio_ns_bind(ns, "/////foo", ch0.release()));

  ASSERT_OK(fdio_ns_unbind(ns, "/foo/fake_subdir/../"));

  ASSERT_OK(fdio_ns_destroy(ns));
}

TEST(NamespaceTest, BindOversizedPath) {
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));

  zx::channel ch0, ch1;
  ASSERT_OK(zx::channel::create(0, &ch0, &ch1));

  std::string long_path = "/";
  long_path.append(PATH_MAX - 1, 'a');

  // The largest legal path is PATH_MAX - 1 characters as PATH_MAX include space
  // for a null terminator. This path is thus too long by one character.
  EXPECT_EQ(long_path.length(), PATH_MAX);

  EXPECT_STATUS(fdio_ns_bind(ns, long_path.c_str(), ch0.release()), ZX_ERR_BAD_PATH);

  ASSERT_OK(fdio_ns_destroy(ns));
}

TEST(NamespaceTest, BindOversizedPathComponent) {
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));

  zx::channel ch0, ch1;
  ASSERT_OK(zx::channel::create(0, &ch0, &ch1));

  std::string long_path_component = "/";
  // Path components are limited to up to NAME_MAX characters. This path
  // component is thus too long by one character.
  long_path_component.append(NAME_MAX + 1, 'a');

  EXPECT_STATUS(fdio_ns_bind(ns, long_path_component.c_str(), ch0.release()), ZX_ERR_BAD_PATH);

  ASSERT_OK(fdio_ns_destroy(ns));
}

TEST(NamespaceTest, ConnectNonCanonicalPath) {
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));

  zx::channel ch0, ch1;
  ASSERT_OK(zx::channel::create(0, &ch0, &ch1));

  ASSERT_OK(fdio_ns_bind(ns, "/foo", ch0.release()));

  zx::channel service0, service1;
  ASSERT_OK(zx::channel::create(0, &service0, &service1));
  ASSERT_OK(fdio_ns_open(ns, "//foo/fake_subdir/.././Service", 1u, service0.release()));

  // Expect an incoming connect on ch1
  ASSERT_OK(fdio_ns_destroy(ns));
}

TEST(NamespaceTest, ConnectOversizedPath) {
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));

  zx::channel ch0, ch1;
  ASSERT_OK(zx::channel::create(0, &ch0, &ch1));

  std::string long_path = "/";
  long_path.append(PATH_MAX - 1, 'a');

  // The largest legal path is PATH_MAX - 1 characters as PATH_MAX include space
  // for a null terminator. This path is thus too long by one character.
  EXPECT_EQ(long_path.length(), PATH_MAX);

  EXPECT_STATUS(fdio_ns_open(ns, long_path.c_str(), 0u, ch0.release()), ZX_ERR_BAD_PATH);

  ASSERT_OK(fdio_ns_destroy(ns));
}

TEST(NamespaceTest, ConnectOversizedPathComponent) {
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));

  zx::channel ch0, ch1;
  ASSERT_OK(zx::channel::create(0, &ch0, &ch1));

  std::string long_path_component = "/";
  // Path components are limited to up to NAME_MAX characters. This path
  // component is thus too long by one character.
  long_path_component.append(NAME_MAX + 1, 'a');

  EXPECT_STATUS(fdio_ns_open(ns, long_path_component.c_str(), 0u, ch0.release()), ZX_ERR_BAD_PATH);

  ASSERT_OK(fdio_ns_destroy(ns));
}
