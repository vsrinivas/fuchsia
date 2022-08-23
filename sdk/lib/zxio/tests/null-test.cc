// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/null.h>
#include <lib/zxio/zxio.h>

#include <zxtest/zxtest.h>

TEST(NullTest, Default) {
  zxio_t io;
  zxio_default_init(&io);

  zxio_signals_t observed = ZXIO_SIGNAL_NONE;
  ASSERT_STATUS(zxio_wait_one(&io, ZXIO_SIGNAL_READABLE, ZX_TIME_INFINITE, &observed),
                ZX_ERR_NOT_SUPPORTED);

  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_STATUS(zxio_clone(&io, &handle), ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(ZX_HANDLE_INVALID, handle);

  ASSERT_STATUS(zxio_sync(&io), ZX_ERR_NOT_SUPPORTED);

  zxio_node_attributes_t attr = {};
  ASSERT_STATUS(zxio_attr_get(&io, &attr), ZX_ERR_NOT_SUPPORTED);
  ASSERT_STATUS(zxio_attr_set(&io, &attr), ZX_ERR_NOT_SUPPORTED);
  char buffer[1024];
  memset(buffer, 0, sizeof(buffer));
  size_t actual = 5u;
  ASSERT_STATUS(zxio_read(&io, buffer, sizeof(buffer), 0, &actual), ZX_ERR_WRONG_TYPE);
  ASSERT_STATUS(zxio_read_at(&io, 0u, buffer, sizeof(buffer), 0, &actual), ZX_ERR_WRONG_TYPE);
  ASSERT_STATUS(zxio_write(&io, buffer, sizeof(buffer), 0, &actual), ZX_ERR_WRONG_TYPE);
  ASSERT_STATUS(zxio_write_at(&io, 0u, buffer, sizeof(buffer), 0, &actual), ZX_ERR_WRONG_TYPE);
  size_t offset = 0u;
  ASSERT_STATUS(zxio_seek(&io, ZXIO_SEEK_ORIGIN_START, 0, &offset), ZX_ERR_WRONG_TYPE);
  ASSERT_STATUS(zxio_truncate(&io, 0u), ZX_ERR_NOT_SUPPORTED);
  uint32_t flags = 0u;
  ASSERT_STATUS(zxio_flags_get(&io, &flags), ZX_ERR_NOT_SUPPORTED);
  ASSERT_STATUS(zxio_flags_set(&io, flags), ZX_ERR_NOT_SUPPORTED);
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  ASSERT_STATUS(zxio_vmo_get_copy(&io, &vmo), ZX_ERR_NOT_SUPPORTED);
  ASSERT_STATUS(zxio_vmo_get_clone(&io, &vmo), ZX_ERR_NOT_SUPPORTED);
  ASSERT_STATUS(zxio_vmo_get_exact(&io, &vmo), ZX_ERR_NOT_SUPPORTED);

  ASSERT_STATUS(zxio_default_get_read_buffer_available(&io, nullptr), ZX_ERR_NOT_SUPPORTED);

  constexpr std::string_view name("hello");
  ASSERT_STATUS(zxio_open_async(&io, 0u, 0u, name.data(), name.length(), ZX_HANDLE_INVALID),
                ZX_ERR_NOT_SUPPORTED);
  ASSERT_STATUS(zxio_add_inotify_filter(&io, name.data(), name.length(), 0u, 0, ZX_HANDLE_INVALID),
                ZX_ERR_NOT_SUPPORTED);
  ASSERT_STATUS(zxio_unlink(&io, name.data(), name.length(), 0), ZX_ERR_NOT_SUPPORTED);

  constexpr std::string_view old_path("one");
  constexpr std::string_view new_path("two");
  ASSERT_STATUS(zxio_rename(&io, old_path.data(), old_path.length(), ZX_HANDLE_INVALID,
                            new_path.data(), new_path.length()),
                ZX_ERR_NOT_SUPPORTED);
  ASSERT_STATUS(zxio_link(&io, old_path.data(), old_path.length(), ZX_HANDLE_INVALID,
                          new_path.data(), new_path.length()),
                ZX_ERR_NOT_SUPPORTED);

  zxio_dirent_iterator_t iterator = {};
  ASSERT_STATUS(zxio_dirent_iterator_init(&iterator, &io), ZX_ERR_NOT_SUPPORTED);

  char name_buffer[ZXIO_MAX_FILENAME + 1];
  zxio_dirent_t entry = {.name = name_buffer};
  ASSERT_STATUS(zxio_dirent_iterator_next(&iterator, &entry), ZX_ERR_NOT_SUPPORTED);

  ASSERT_OK(zxio_close(&io));
}

TEST(NullTest, Null) {
  zxio_t io;
  zxio_null_init(&io);

  zxio_signals_t observed = ZXIO_SIGNAL_NONE;
  ASSERT_STATUS(zxio_wait_one(&io, ZXIO_SIGNAL_READABLE, ZX_TIME_INFINITE, &observed),
                ZX_ERR_NOT_SUPPORTED);

  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_STATUS(zxio_clone(&io, &handle), ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(ZX_HANDLE_INVALID, handle);

  ASSERT_STATUS(zxio_sync(&io), ZX_ERR_NOT_SUPPORTED);

  zxio_node_attributes_t attr = {};
  ASSERT_STATUS(zxio_attr_get(&io, &attr), ZX_ERR_NOT_SUPPORTED);
  ASSERT_STATUS(zxio_attr_set(&io, &attr), ZX_ERR_NOT_SUPPORTED);
  char buffer[1024];
  memset(buffer, 0, sizeof(buffer));
  size_t actual = 5u;
  ASSERT_OK(zxio_read(&io, buffer, sizeof(buffer), 0, &actual));
  EXPECT_EQ(0u, actual);
  ASSERT_STATUS(zxio_read_at(&io, 0u, buffer, sizeof(buffer), 0, &actual), ZX_ERR_WRONG_TYPE);
  ASSERT_OK(zxio_write(&io, buffer, sizeof(buffer), 0, &actual));
  EXPECT_EQ(sizeof(buffer), actual);
  ASSERT_STATUS(zxio_write_at(&io, 0u, buffer, sizeof(buffer), 0, &actual), ZX_ERR_WRONG_TYPE);
  size_t offset = 0u;
  ASSERT_STATUS(zxio_seek(&io, ZXIO_SEEK_ORIGIN_START, 0, &offset), ZX_ERR_WRONG_TYPE);
  ASSERT_STATUS(zxio_truncate(&io, 0u), ZX_ERR_NOT_SUPPORTED);
  uint32_t flags = 0u;
  ASSERT_STATUS(zxio_flags_get(&io, &flags), ZX_ERR_NOT_SUPPORTED);
  ASSERT_STATUS(zxio_flags_set(&io, flags), ZX_ERR_NOT_SUPPORTED);
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  ASSERT_STATUS(zxio_vmo_get_copy(&io, &vmo), ZX_ERR_NOT_SUPPORTED);
  ASSERT_STATUS(zxio_vmo_get_clone(&io, &vmo), ZX_ERR_NOT_SUPPORTED);
  ASSERT_STATUS(zxio_vmo_get_exact(&io, &vmo), ZX_ERR_NOT_SUPPORTED);

  ASSERT_STATUS(zxio_default_get_read_buffer_available(&io, nullptr), ZX_ERR_NOT_SUPPORTED);

  constexpr std::string_view name("hello");
  ASSERT_STATUS(zxio_open_async(&io, 0u, 0u, name.data(), name.length(), ZX_HANDLE_INVALID),
                ZX_ERR_NOT_SUPPORTED);
  ASSERT_STATUS(zxio_add_inotify_filter(&io, name.data(), name.length(), 0u, 0, ZX_HANDLE_INVALID),
                ZX_ERR_NOT_SUPPORTED);
  ASSERT_STATUS(zxio_unlink(&io, name.data(), name.length(), 0), ZX_ERR_NOT_SUPPORTED);

  constexpr std::string_view old_path("one");
  constexpr std::string_view new_path("two");
  ASSERT_STATUS(zxio_rename(&io, old_path.data(), old_path.length(), ZX_HANDLE_INVALID,
                            new_path.data(), new_path.length()),
                ZX_ERR_NOT_SUPPORTED);
  ASSERT_STATUS(zxio_link(&io, old_path.data(), old_path.length(), ZX_HANDLE_INVALID,
                          new_path.data(), new_path.length()),
                ZX_ERR_NOT_SUPPORTED);

  zxio_dirent_iterator_t iterator = {};
  ASSERT_STATUS(zxio_dirent_iterator_init(&iterator, &io), ZX_ERR_NOT_SUPPORTED);

  char name_buffer[ZXIO_MAX_FILENAME + 1];
  zxio_dirent_t entry = {.name = name_buffer};
  ASSERT_STATUS(zxio_dirent_iterator_next(&iterator, &entry), ZX_ERR_NOT_SUPPORTED);

  ASSERT_OK(zxio_close(&io));
}

TEST(NullTest, DefaultRelease) {
  zxio_t io;
  zxio_default_init(&io);
  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_STATUS(zxio_release(&io, &handle), ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(ZX_HANDLE_INVALID, handle);
  ASSERT_OK(zxio_close(&io));
}

TEST(NullTest, NullRelease) {
  zxio_t io;
  zxio_null_init(&io);
  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_STATUS(zxio_release(&io, &handle), ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(ZX_HANDLE_INVALID, handle);
  ASSERT_OK(zxio_close(&io));
}
