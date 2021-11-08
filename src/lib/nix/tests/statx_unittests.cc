// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/statx.h>
#include <sys/types.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

TEST(StatxTest, BadPathName) {
  struct statx buf;
  ASSERT_EQ(-1, statx(AT_FDCWD, 0, 0, STATX_BASIC_STATS, &buf));
  ASSERT_EQ(EFAULT, errno);
}

TEST(StatxTest, BadBuffer) {
  char pathname[] = "/tmp/statx_badbuffer_test.XXXXXX";
  fbl::unique_fd src_file(mkstemp(pathname));
  ASSERT_TRUE(src_file.is_valid());
  close(src_file.get());
  ASSERT_EQ(-1, statx(AT_FDCWD, pathname, 0, STATX_BASIC_STATS, 0));
  ASSERT_EQ(EFAULT, errno);
}

TEST(StatxTest, BasicStatxCheck) {
  char pathname[] = "/tmp/statx_basic_test.XXXXXX";
  struct statx statx_buf;
  struct stat stat_buf;
  fbl::unique_fd src_file(mkstemp(pathname));
  ASSERT_TRUE(src_file.is_valid());
  close(src_file.get());
  ASSERT_EQ(0, statx(AT_FDCWD, pathname, 0, STATX_BASIC_STATS, &statx_buf));

  ASSERT_EQ(0, stat(pathname, &stat_buf));

  // Compare results with stat
  ASSERT_EQ(statx_buf.stx_ino, stat_buf.st_ino);
  ASSERT_EQ(statx_buf.stx_nlink, stat_buf.st_nlink);
  ASSERT_EQ(statx_buf.stx_size, stat_buf.st_size);
  ASSERT_EQ(statx_buf.stx_mode, stat_buf.st_mode);
}

TEST(StatxTest, BasicStatxMaskCheck) {
  char pathname[] = "/tmp/statx_basic_mask_test.XXXXXX";
  struct statx statx_buf;
  struct stat stat_buf;
  fbl::unique_fd src_file(mkstemp(pathname));
  ASSERT_TRUE(src_file.is_valid());
  close(src_file.get());
  ASSERT_EQ(0, statx(AT_FDCWD, pathname, 0, STATX_NLINK, &statx_buf));

  ASSERT_EQ(0, stat(pathname, &stat_buf));

  // Compare results with stat
  ASSERT_EQ(statx_buf.stx_nlink, stat_buf.st_nlink);

  // Check if correct mask was received.
  ASSERT_EQ(statx_buf.stx_mask & STATX_NLINK, STATX_NLINK);

  // Check if other values were not set.
  ASSERT_NE(statx_buf.stx_ino, stat_buf.st_ino);
  ASSERT_NE(statx_buf.stx_mode, stat_buf.st_mode);
}
