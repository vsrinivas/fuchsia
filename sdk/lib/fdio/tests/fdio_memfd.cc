// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/mman.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

TEST(MemFDTest, Smoke) {
  fbl::unique_fd fd(memfd_create(nullptr, 0));
  EXPECT_TRUE(fd.is_valid());
  constexpr char payload[] = "abc";
  EXPECT_EQ(write(fd.get(), payload, sizeof(payload)), ssize_t(sizeof(payload)));
  char buffer[sizeof(payload) + 1] = {};
  EXPECT_EQ(pread(fd.get(), buffer, sizeof(buffer), 0), ssize_t(sizeof(payload)));
  EXPECT_STREQ(buffer, payload);
}

TEST(MemFDTest, SeekPastEnd) {
  fbl::unique_fd fd(memfd_create(nullptr, 0));
  EXPECT_TRUE(fd.is_valid());
  constexpr char payload1[] = "abc";
  EXPECT_EQ(write(fd.get(), payload1, sizeof(payload1)), ssize_t(sizeof(payload1)));
  ASSERT_EQ(10543, lseek(fd.get(), 10543, SEEK_SET));
  constexpr char payload2[] = "xyz";
  EXPECT_EQ(write(fd.get(), payload2, sizeof(payload2)), ssize_t(sizeof(payload2)));

  char buffer[std::max(sizeof(payload1), sizeof(payload2)) + 1] = {};
  EXPECT_EQ(pread(fd.get(), buffer, sizeof(buffer), 10543), ssize_t(sizeof(payload2)));
  EXPECT_STREQ(buffer, payload2);
}

TEST(MemFDTest, Truncate) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(memfd_create(nullptr, 0)), "%s", strerror(errno));
  constexpr off_t len = 10544;
  EXPECT_SUCCESS(ftruncate(fd.get(), len));
  constexpr char payload1[] = "xyz";
  ASSERT_EQ(lseek(fd.get(), -sizeof(payload1), SEEK_END), len - ssize_t(sizeof(payload1)), "%s",
            strerror(errno));
  EXPECT_EQ(write(fd.get(), payload1, sizeof(payload1)), ssize_t(sizeof(payload1)), "%s",
            strerror(errno));
  ASSERT_EQ(lseek(fd.get(), -5014, SEEK_CUR), len - 5014, "%s", strerror(errno));
  constexpr char payload2[] = "abc";
  EXPECT_EQ(write(fd.get(), payload2, sizeof(payload2)), ssize_t(sizeof(payload2)), "%s",
            strerror(errno));

  char buffer[std::max(sizeof(payload1), sizeof(payload2)) + 1] = {};
  EXPECT_EQ(pread(fd.get(), buffer, sizeof(buffer), len - 5014), ssize_t(sizeof(buffer)), "%s",
            strerror(errno));
  EXPECT_STREQ(buffer, payload2);

  memset(buffer, 0, sizeof(buffer));
  EXPECT_EQ(pread(fd.get(), buffer, sizeof(buffer), len - sizeof(payload1)),
            ssize_t(sizeof(payload1)), "%s", strerror(errno));
  EXPECT_STREQ(buffer, payload1);

  EXPECT_SUCCESS(ftruncate(fd.get(), len - 5014));
  EXPECT_SUCCESS(ftruncate(fd.get(), len));

  memset(buffer, 0, sizeof(buffer));
  EXPECT_EQ(pread(fd.get(), buffer, sizeof(buffer), 5530), ssize_t(sizeof(buffer)), "%s",
            strerror(errno));
  char zeros[sizeof(buffer)] = {};
  EXPECT_EQ(memcmp(buffer, zeros, sizeof(buffer)), 0);

  memset(buffer, 0, sizeof(buffer));
  EXPECT_EQ(pread(fd.get(), buffer, sizeof(buffer), len - sizeof(payload1)),
            ssize_t(sizeof(payload1)), "%s", strerror(errno));
  EXPECT_EQ(memcmp(buffer, zeros, sizeof(payload1)), 0);
}

TEST(MemFDTest, MMap) {
  const size_t kSize = 256;

  fbl::unique_fd fd(memfd_create(nullptr, 0));
  EXPECT_TRUE(fd.is_valid());
  EXPECT_EQ(0, ftruncate(fd.get(), kSize));
  constexpr char payload[] = "abc";
  EXPECT_EQ(write(fd.get(), payload, sizeof(payload)), ssize_t(sizeof(payload)));

  void* ptr = mmap(nullptr, kSize, PROT_READ, MAP_SHARED, fd.get(), 0u);
  EXPECT_NE(MAP_FAILED, ptr);
  char buffer[sizeof(payload) + 1] = {};
  memcpy(buffer, ptr, sizeof(buffer));
  EXPECT_STREQ(payload, buffer);
  ASSERT_EQ(0, munmap(ptr, kSize));
}
