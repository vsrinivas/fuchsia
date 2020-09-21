// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/null.h>
#include <lib/zxio/zxio.h>

#include <zxtest/zxtest.h>

TEST(NullTest, Basic) {
  zxio_t io;
  zxio_null_init(&io);

  zxio_signals_t observed = ZXIO_SIGNAL_NONE;
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED,
            zxio_wait_one(&io, ZXIO_SIGNAL_READABLE, ZX_TIME_INFINITE, &observed));

  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_clone(&io, &handle));
  ASSERT_EQ(ZX_HANDLE_INVALID, handle);

  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_sync(&io));

  zxio_node_attributes_t attr = {};
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_attr_get(&io, &attr));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_attr_set(&io, &attr));
  char buffer[1024];
  memset(buffer, 0, sizeof(buffer));
  size_t actual = 5u;
  ASSERT_OK(zxio_read(&io, buffer, sizeof(buffer), 0, &actual));
  EXPECT_EQ(0u, actual);
  ASSERT_EQ(ZX_ERR_WRONG_TYPE, zxio_read_at(&io, 0u, buffer, sizeof(buffer), 0, &actual));
  ASSERT_OK(zxio_write(&io, buffer, sizeof(buffer), 0, &actual));
  EXPECT_EQ(sizeof(buffer), actual);
  ASSERT_EQ(ZX_ERR_WRONG_TYPE, zxio_write_at(&io, 0u, buffer, sizeof(buffer), 0, &actual));
  size_t offset = 0u;
  ASSERT_EQ(ZX_ERR_WRONG_TYPE, zxio_seek(&io, ZXIO_SEEK_ORIGIN_START, 0, &offset));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_truncate(&io, 0u));
  uint32_t flags = 0u;
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_flags_get(&io, &flags));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_flags_set(&io, flags));
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  size_t size = 0u;
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_vmo_get_copy(&io, &vmo, &size));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_vmo_get_clone(&io, &vmo, &size));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_vmo_get_exact(&io, &vmo, &size));

  zxio_t* result = nullptr;
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_open(&io, 0u, 0u, "hello", &result));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED,
            zxio_open_async(&io, 0u, 0u, "hello", strlen("hello"), ZX_HANDLE_INVALID));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_unlink(&io, "hello"));

  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_rename(&io, "one", ZX_HANDLE_INVALID, "two"));
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_link(&io, "one", ZX_HANDLE_INVALID, "two"));

  zxio_dirent_iterator_t iter = {};
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_dirent_iterator_init(&iter, &io));
  zxio_dirent_t* entry = nullptr;
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_dirent_iterator_next(&iter, &entry));

  ASSERT_OK(zxio_close(&io));
}

TEST(NullTest, Release) {
  zxio_t io;
  zxio_null_init(&io);
  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zxio_release(&io, &handle));
  ASSERT_EQ(ZX_HANDLE_INVALID, handle);
  ASSERT_OK(zxio_close(&io));
}
