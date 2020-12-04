// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

// TODO(60236): get the memfd_create symbol from libc instead.
#include "memfd.h"

TEST(MemFDTest, Smoke) {
  fbl::unique_fd fd(memfd_create(nullptr, 0));
  EXPECT_TRUE(fd.is_valid());
  EXPECT_EQ(3, write(fd.get(), "abc", 3));
  char buffer[1024] = {};
  EXPECT_EQ(3, pread(fd.get(), buffer, sizeof(buffer), 0));
  EXPECT_STR_EQ("abc", buffer);
}

TEST(MemFDTest, SeekPastEnd) {
  fbl::unique_fd fd(memfd_create(nullptr, 0));
  EXPECT_TRUE(fd.is_valid());
  EXPECT_EQ(3, write(fd.get(), "abc", 3));
  ASSERT_EQ(10543, lseek(fd.get(), 10543, SEEK_SET));
  EXPECT_EQ(3, write(fd.get(), "xyz", 3));

  char buffer[1024] = {};
  EXPECT_EQ(3, pread(fd.get(), buffer, sizeof(buffer), 10543));
  EXPECT_STR_EQ("xyz", buffer);
}

TEST(MemFDTest, Truncate) {
  fbl::unique_fd fd(memfd_create(nullptr, 0));
  EXPECT_TRUE(fd.is_valid());
  EXPECT_EQ(0, ftruncate(fd.get(), 10543));
  ASSERT_EQ(10540, lseek(fd.get(), -3, SEEK_END));
  EXPECT_EQ(3, write(fd.get(), "xyz", 3));
  ASSERT_EQ(5530, lseek(fd.get(), -5013, SEEK_CUR));
  EXPECT_EQ(3, write(fd.get(), "abc", 3));

  char buffer[1024] = {};
  EXPECT_EQ(sizeof(buffer), pread(fd.get(), buffer, sizeof(buffer), 5530));
  EXPECT_STR_EQ("abc", buffer);

  memset(buffer, 0, sizeof(buffer));
  EXPECT_EQ(3, pread(fd.get(), buffer, sizeof(buffer), 10540));
  EXPECT_STR_EQ("xyz", buffer);

  EXPECT_EQ(0, ftruncate(fd.get(), 5530));
  EXPECT_EQ(0, ftruncate(fd.get(), 10543));

  memset(buffer, 0, sizeof(buffer));
  EXPECT_EQ(sizeof(buffer), pread(fd.get(), buffer, sizeof(buffer), 5530));
  char zeros[sizeof(buffer)] = {};
  EXPECT_EQ(0, memcmp(buffer, zeros, sizeof(buffer)));

  memset(buffer, 0, sizeof(buffer));
  EXPECT_EQ(3, pread(fd.get(), buffer, sizeof(buffer), 10540));
  EXPECT_EQ(0, memcmp(buffer, zeros, 3));
}
