// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

TEST(UnistdTest, TruncateWithNegativeLength) {
  const char* filename = "/tmp/truncate-with-negative-length-test";
  fbl::unique_fd fd(open(filename, O_CREAT | O_RDWR, 0666));
  ASSERT_TRUE(fd);
  EXPECT_EQ(-1, ftruncate(fd.get(), -1));
  EXPECT_EQ(EINVAL, errno);
  EXPECT_EQ(-1, ftruncate(fd.get(), std::numeric_limits<off_t>::min()));
  EXPECT_EQ(EINVAL, errno);

  EXPECT_EQ(-1, truncate(filename, -1));
  EXPECT_EQ(EINVAL, errno);
  EXPECT_EQ(-1, truncate(filename, std::numeric_limits<off_t>::min()));
  EXPECT_EQ(EINVAL, errno);
}

}  // namespace
