// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_fd.h>
#include <limits.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <unistd.h>

#include <zxtest/zxtest.h>

TEST(InotifyTest, Unsupported) {
  fbl::unique_fd fd(inotify_init());
#if __Fuchsia__
  EXPECT_FALSE(fd.is_valid());
  ASSERT_EQ(ENOSYS, errno, "errno incorrect");
#else
  EXPECT_TRUE(fd.is_valid());
#endif
}
