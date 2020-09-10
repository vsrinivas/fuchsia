// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/zxio.h>
#include <limits.h>
#include <zircon/compiler.h>

#include <zxtest/zxtest.h>

constexpr size_t kSize = 300;
constexpr zx_off_t kInitialSeek = 4;

constexpr const char* ALPHABET = "abcdefghijklmnopqrstuvwxyz";

class VmoTest : public zxtest::Test {
 public:
  void SetUp() override {
    ASSERT_OK(zx::vmo::create(kSize, 0u, &backing));
    ASSERT_OK(backing.write(ALPHABET, 0, len));
    ASSERT_OK(backing.write(ALPHABET, len, len + len));

    ASSERT_OK(zxio_vmo_init(&storage, std::move(backing), kInitialSeek));
    io = &storage.io;
  }

  void TearDown() override {
    ASSERT_OK(zxio_close(io));
    ASSERT_OK(zxio_destroy(io));
  }

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

  zxio_node_attributes_t attr = {};
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

TEST_F(VmoTest, SeekNegativeOverflow) {
  // We set up a large negative seek (larger than the page-rounded-up size of the VMO underlying
  // us).
  constexpr int64_t kTooFarBackwards = -8192;

  // Seek somewhere slightly more random than the start.
  size_t original_seek = 23;
  size_t new_seek = 42;
  ASSERT_OK(zxio_seek(io, ZXIO_SEEK_ORIGIN_START, original_seek, &new_seek));
  ASSERT_EQ(original_seek, new_seek);

  // Seeking backwards from the start past zero should fail, without moving the seek pointer.
  ASSERT_STATUS(ZX_ERR_OUT_OF_RANGE,
                zxio_seek(io, ZXIO_SEEK_ORIGIN_START, kTooFarBackwards, &new_seek));
  ASSERT_OK(zxio_seek(io, ZXIO_SEEK_ORIGIN_CURRENT, 0, &new_seek));
  ASSERT_EQ(original_seek, new_seek);

  new_seek = 42;

  // Seeking backwards from the seek pointer past zero should fail, without moving the seek pointer.
  ASSERT_STATUS(ZX_ERR_OUT_OF_RANGE,
                zxio_seek(io, ZXIO_SEEK_ORIGIN_CURRENT, kTooFarBackwards, &new_seek));
  ASSERT_OK(zxio_seek(io, ZXIO_SEEK_ORIGIN_CURRENT, 0, &new_seek));
  ASSERT_EQ(original_seek, new_seek);

  new_seek = 42;

  // Seeking backwards from the end past zero should fail, without moving the seek pointer.
  ASSERT_STATUS(ZX_ERR_OUT_OF_RANGE,
                zxio_seek(io, ZXIO_SEEK_ORIGIN_END, kTooFarBackwards, &new_seek));
  ASSERT_OK(zxio_seek(io, ZXIO_SEEK_ORIGIN_CURRENT, 0, &new_seek));
  ASSERT_EQ(original_seek, new_seek);
}

// This sets up the same test case, but with a huge backing VMO. Specifically, the backing VMO needs
// to be large enough that adding a signed 64 bit value to its length is large enough to overflow an
// unsigned 64 bit value.

constexpr size_t kEighthOfMax = 0x2000000000000000;
static_assert(kEighthOfMax * 8 == 0);
constexpr size_t kHugeSize = kEighthOfMax * 7;

class HugeVmoTest : public zxtest::Test {
 public:
  void SetUp() override {
    ASSERT_OK(zx::vmo::create(kHugeSize, 0u, &backing));
    ASSERT_OK(backing.write(ALPHABET, 0, len));
    ASSERT_OK(backing.write(ALPHABET, len, len + len));

    ASSERT_OK(zxio_vmo_init(&storage, std::move(backing), 0));
    io = &storage.io;
  }

  void TearDown() override { ASSERT_OK(zxio_close(io)); }

 protected:
  zx::vmo backing;
  size_t len = strlen(ALPHABET);
  zxio_storage_t storage;
  zxio_t* io;
};

TEST_F(HugeVmoTest, SeekPositiveOverflow) {
  // Check that our expected-to-overflow values actually overflow.
  constexpr int64_t kTooFarForwards = kEighthOfMax * 2;
  static_assert(kHugeSize + kTooFarForwards < kHugeSize);

  // Seek to the end.
  size_t original_seek;
  ASSERT_OK(zxio_seek(io, ZXIO_SEEK_ORIGIN_END, 0, &original_seek));
  ASSERT_EQ(original_seek, kHugeSize);

  constexpr size_t dummy_seek = 42;

  size_t new_seek = dummy_seek;

  // There's no tests for seeking forwards from the start of the file past infinity, since a int64_t
  // isn't big enough to cause the overflow.

  // Seeking forward from the seek pointer past past infinity should fail, without moving the seek
  // pointer.
  ASSERT_STATUS(ZX_ERR_OUT_OF_RANGE,
                zxio_seek(io, ZXIO_SEEK_ORIGIN_CURRENT, kTooFarForwards, &new_seek));
  ASSERT_OK(zxio_seek(io, ZXIO_SEEK_ORIGIN_CURRENT, 0, &new_seek));
  ASSERT_EQ(original_seek, new_seek);

  new_seek = 42;

  // Seeking forward from the end past past infinity should fail, without moving the seek
  // pointer.
  ASSERT_STATUS(ZX_ERR_OUT_OF_RANGE,
                zxio_seek(io, ZXIO_SEEK_ORIGIN_END, kTooFarForwards, &new_seek));
  ASSERT_OK(zxio_seek(io, ZXIO_SEEK_ORIGIN_CURRENT, 0, &new_seek));
  ASSERT_EQ(original_seek, new_seek);
}

class VmoCloseTest : public VmoTest {
 public:
  void TearDown() override { /* The test case body will exercise closing and destroying */
  }
};

TEST_F(VmoCloseTest, UseAfterClose) {
  char buffer[16] = {};
  size_t actual = 0u;
  ASSERT_OK(zxio_read_at(io, 0, buffer, 6, 0, &actual));
  EXPECT_EQ(actual, 6);

  ASSERT_OK(zxio_close(io));
  actual = 0;
  ASSERT_EQ(ZX_ERR_BAD_HANDLE, zxio_read_at(io, 0, buffer, 6, 0, &actual));
  EXPECT_EQ(actual, 0);

  ASSERT_OK(zxio_destroy(io));
}
