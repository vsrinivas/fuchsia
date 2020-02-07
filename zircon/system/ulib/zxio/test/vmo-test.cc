// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/zxio.h>
#include <limits.h>

#include <zxtest/zxtest.h>

constexpr const char* ALPHABET = "abcdefghijklmnopqrstuvwxyz";

class VmoTest : public zxtest::Test {
 public:
  void SetUp() override {
    ASSERT_OK(zx::vmo::create(300u, 0u, &backing));
    ASSERT_OK(backing.write(ALPHABET, 0, len));
    ASSERT_OK(backing.write(ALPHABET, len, len + len));

    zxio_vmo_init(&storage, std::move(backing), /* initial seek */ 4);
    io = &storage.io;
  }

  void TearDown() override { ASSERT_OK(zxio_close(io)); }

 protected:
  zx::vmo backing;
  size_t len = strlen(ALPHABET);
  zxio_storage_t storage;
  zxio_t* io;
};

TEST_F(VmoTest, Basic) {
  zxio_signals_t observed = ZXIO_SIGNAL_NONE;
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED,
                zxio_wait_one(io, ZXIO_SIGNAL_READABLE, ZX_TIME_INFINITE, &observed));

  zx::channel clone;
  ASSERT_OK(zxio_clone(io, clone.reset_and_get_address()));
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, zxio_sync(io));

  zxio_node_attr_t attr = {};
  ASSERT_OK(zxio_attr_get(io, &attr));
  EXPECT_EQ(PAGE_SIZE, attr.content_size);
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, zxio_attr_set(io, &attr));

  char buffer[1024];
  memset(buffer, 0, sizeof(buffer));
  size_t actual = 0u;
  ASSERT_OK(zxio_read(io, buffer, 8, 0, &actual));
  EXPECT_EQ(actual, 8);
  EXPECT_STR_EQ("efghijkl", buffer);
  memset(buffer, 0, sizeof(buffer));
  actual = 0u;
  ASSERT_OK(zxio_read_at(io, 1u, buffer, 6, 0, &actual));
  EXPECT_EQ(actual, 6);
  EXPECT_STR_EQ("bcdefg", buffer);

  size_t offset = 2u;
  ASSERT_OK(zxio_seek(io, ZXIO_SEEK_ORIGIN_START, 2, &offset));
  EXPECT_EQ(offset, 2u);
  memset(buffer, 0, sizeof(buffer));
  actual = 0u;
  ASSERT_OK(zxio_read(io, buffer, 3, 0, &actual));
  EXPECT_STR_EQ("cde", buffer);
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, zxio_truncate(io, 0u));
  uint32_t flags = 0u;
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, zxio_flags_get(io, &flags));
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, zxio_flags_set(io, flags));

  ASSERT_OK(zxio_write(io, buffer, sizeof(buffer), 0, &actual));
  EXPECT_EQ(actual, sizeof(buffer));
  ASSERT_OK(zxio_write_at(io, 0u, buffer, sizeof(buffer), 0, &actual));
  EXPECT_EQ(actual, sizeof(buffer));

  zxio_t* result = nullptr;
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, zxio_open(io, 0u, 0u, "hello", &result));
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED,
                zxio_open_async(io, 0u, 0u, "hello", strlen("hello"), ZX_HANDLE_INVALID));
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, zxio_unlink(io, "hello"));
}

TEST_F(VmoTest, GetCopy) {
  zx::vmo vmo;
  size_t size = 0u;
  ASSERT_OK(zxio_vmo_get_copy(io, vmo.reset_and_get_address(), &size));
  EXPECT_NE(vmo, ZX_HANDLE_INVALID);
  EXPECT_EQ(size, PAGE_SIZE);
}

TEST_F(VmoTest, GetClone) {
  zx::vmo vmo;
  size_t size = 0u;
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, zxio_vmo_get_clone(io, vmo.reset_and_get_address(), &size));
}

TEST_F(VmoTest, GetExact) {
  zx::vmo vmo;
  size_t size = 0u;
  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, zxio_vmo_get_exact(io, vmo.reset_and_get_address(), &size));
}
