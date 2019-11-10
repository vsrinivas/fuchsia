// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/ops.h>
#include <lib/zxio/zxio.h>
#include <string.h>

#include <zxtest/zxtest.h>

constexpr int kEntryCount = 30;

static char g_buffer[ZXIO_DIRENT_ITERATOR_DEFAULT_BUFFER_SIZE];
static int g_index;

static void dirent_test(int capacity) {
  zxio_ops_t ops;
  memset(&ops, 0, sizeof(ops));

  g_index = 0;

  ops.readdir = [](zxio_t* io, void* buffer, size_t capacity, size_t* out_actual) {
    auto buffer_start = reinterpret_cast<uint8_t*>(buffer);

    *out_actual = 0;

    for (; g_index < kEntryCount; g_index++) {
      const size_t name_length = g_index + 1;

      auto buffer_position = buffer_start + (*out_actual);

      auto entry = reinterpret_cast<zxio_dirent_t*>(buffer_position);
      size_t entry_size = sizeof(zxio_dirent_t) + name_length;

      if (buffer_position - buffer_start + entry_size > capacity) {
        return ZX_OK;
      }

      auto name = new char[name_length + 1];
      snprintf(name, name_length + 1, "%0*d", static_cast<int>(name_length), g_index);
      // No null termination
      memcpy(entry->name, name, name_length);
      delete[] name;

      if (name_length > UINT8_MAX) {
        return ZX_ERR_BAD_STATE;
      }
      entry->size = static_cast<uint8_t>(name_length);
      entry->inode = g_index;

      *out_actual += entry_size;
    }
    return ZX_OK;
  };

  zxio_t io = {};
  zxio_init(&io, &ops);

  zxio_dirent_iterator_t iterator;
  ASSERT_OK(zxio_dirent_iterator_init(&iterator, &io, g_buffer, capacity));

  for (int count = 0; count < kEntryCount; count++) {
    zxio_dirent_t* entry;
    EXPECT_OK(zxio_dirent_iterator_next(&iterator, &entry));
    EXPECT_EQ(entry->inode, count);
    EXPECT_EQ(entry->size, count + 1);
  }
}

TEST(DirentTest, standard_buffer_size) { dirent_test(sizeof(g_buffer)); }

TEST(DirentTest, small_buffer_size) {
  // Just enough for the longest entry
  dirent_test(sizeof(zxio_dirent_t) + kEntryCount + 1);
}

TEST(DirentTest, other_buffer_size) { dirent_test(sizeof(zxio_dirent_t) + kEntryCount * 3); }
