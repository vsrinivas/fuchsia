// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/nand/drivers/nand/read_cache.h"

#include <string.h>

#include <zxtest/zxtest.h>

namespace nand {

namespace {

constexpr uint32_t kCacheSize = 5;
constexpr size_t kDataSize = 16;
constexpr size_t kSpareSize = 2;

TEST(ReadCacheTest, BasicInsertRetrievalAndPurge) {
  ReadCache cache(kCacheSize, kDataSize, kSpareSize);
  uint8_t data_buf[kDataSize];
  uint8_t spare_buf[kSpareSize];
  // Not present.
  ASSERT_EQ(cache.GetPage(7, data_buf, spare_buf), 0);

  data_buf[0] = 'a';
  data_buf[kDataSize - 1] = 'z';
  spare_buf[0] = '0';
  spare_buf[kSpareSize - 1] = '9';
  cache.Insert(7, data_buf, spare_buf);

  memset(data_buf, 0, kDataSize);
  memset(spare_buf, 0, kSpareSize);
  ASSERT_EQ(cache.GetPage(7, data_buf, spare_buf), 1);
  ASSERT_EQ(data_buf[0], 'a');
  ASSERT_EQ(data_buf[kDataSize - 1], 'z');
  ASSERT_EQ(spare_buf[0], '0');
  ASSERT_EQ(spare_buf[kSpareSize - 1], '9');

  ASSERT_EQ(cache.PurgeRange(7, 1), 1u);

  memset(data_buf, 0, kDataSize);
  memset(spare_buf, 0, kSpareSize);
  ASSERT_EQ(cache.GetPage(7, data_buf, spare_buf), 0);
}

TEST(ReadCacheTest, GetCorrectResult) {
  ReadCache cache(kCacheSize, kDataSize, kSpareSize);
  uint8_t data_buf[kDataSize];
  uint8_t spare_buf[kSpareSize];

  data_buf[0] = 'a';
  spare_buf[0] = '0';
  cache.Insert(7, data_buf, spare_buf);

  data_buf[0] = 'b';
  spare_buf[0] = '1';
  cache.Insert(9, data_buf, spare_buf);

  ASSERT_EQ(cache.GetPage(7, data_buf, spare_buf), 1);
  ASSERT_EQ(data_buf[0], 'a');
  ASSERT_EQ(spare_buf[0], '0');

  ASSERT_EQ(cache.GetPage(9, data_buf, spare_buf), 1);
  ASSERT_EQ(data_buf[0], 'b');
  ASSERT_EQ(spare_buf[0], '1');
}

TEST(ReadCacheTest, PurgeMultiple) {
  ReadCache cache(kCacheSize, kDataSize, kSpareSize);
  uint8_t data_buf[kDataSize];
  uint8_t spare_buf[kSpareSize];

  // Add 4 entries.
  for (uint32_t i = 0; i < 4; i++) {
    data_buf[0] = static_cast<uint8_t>(i);
    spare_buf[0] = static_cast<uint8_t>(i);
    cache.Insert(i, data_buf, spare_buf);
  }

  // They're all there.
  for (uint32_t i = 0; i < 4; i++) {
    ASSERT_EQ(cache.GetPage(i, data_buf, spare_buf), 1);
    ASSERT_EQ(data_buf[0], static_cast<uint8_t>(i));
    ASSERT_EQ(spare_buf[0], static_cast<uint8_t>(i));
  }

  // Purge 2 in the middle.
  ASSERT_EQ(cache.PurgeRange(1, 2), 2u);

  // They're gone.
  ASSERT_EQ(cache.GetPage(1, data_buf, spare_buf), 0);
  ASSERT_EQ(cache.GetPage(2, data_buf, spare_buf), 0);

  // The rest remain.
  ASSERT_EQ(cache.GetPage(0, data_buf, spare_buf), 1);
  ASSERT_EQ(data_buf[0], 0u);
  ASSERT_EQ(spare_buf[0], 0u);

  ASSERT_EQ(cache.GetPage(3, data_buf, spare_buf), 1);
  ASSERT_EQ(data_buf[0], 3u);
  ASSERT_EQ(spare_buf[0], 3u);
}

TEST(ReadCacheTest, OverflowEntries) {
  ReadCache cache(kCacheSize, kDataSize, kSpareSize);
  uint8_t data_buf[kDataSize];
  uint8_t spare_buf[kSpareSize];

  // Fill the cache, don't lose the first entry.
  for (uint32_t i = 0; i < kCacheSize; i++) {
    data_buf[0] = static_cast<uint8_t>(i);
    spare_buf[0] = static_cast<uint8_t>(i);
    cache.Insert(i, data_buf, spare_buf);
    ASSERT_EQ(cache.GetPage(0, data_buf, spare_buf), 1);
  }

  // Add one more and we lose the first..
  data_buf[0] = static_cast<uint8_t>(kCacheSize);
  spare_buf[0] = static_cast<uint8_t>(kCacheSize);
  cache.Insert(kCacheSize, data_buf, spare_buf);
  ASSERT_EQ(cache.GetPage(0, data_buf, spare_buf), 0);

  // Verify that the rest are still present.
  for (uint32_t i = 1; i <= kCacheSize; i++) {
    ASSERT_EQ(cache.GetPage(i, data_buf, spare_buf), 1);
    ASSERT_EQ(data_buf[0], static_cast<uint8_t>(i));
    ASSERT_EQ(spare_buf[0], static_cast<uint8_t>(i));
  }
}

// This case shouldn't matter for what we're using this library for, but better to have this make
// intuitive sense by handling this case properly.
TEST(ReadCacheTest, ReinsertCorrectResult) {
  ReadCache cache(kCacheSize, kDataSize, kSpareSize);
  uint8_t data_buf[kDataSize];
  uint8_t spare_buf[kSpareSize];

  // Insert initial copy.
  data_buf[0] = 'a';
  spare_buf[0] = '0';
  cache.Insert(7, data_buf, spare_buf);

  // Overwrite it.
  data_buf[0] = 'b';
  spare_buf[0] = '1';
  cache.Insert(7, data_buf, spare_buf);

  // Get the second version.
  ASSERT_EQ(cache.GetPage(7, data_buf, spare_buf), 1);
  ASSERT_EQ(data_buf[0], 'b');
  ASSERT_EQ(spare_buf[0], '1');

  // Only one to find when removing.
  ASSERT_EQ(cache.PurgeRange(7, 1), 1u);

  // No more copies left.
  ASSERT_EQ(cache.GetPage(7, data_buf, spare_buf), 0);
}

}  // namespace

}  // namespace nand
