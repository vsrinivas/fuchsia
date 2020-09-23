// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>

#include <zxtest/zxtest.h>

TEST(EpollTest, Unsupported) {
  EXPECT_EQ(-1, epoll_create(0));
#if __Fuchsia__
  // Fuchsia currently reports the "wrong" ERRNO for this case because
  // epoll_create is not yet implemented on Fuchsia.
  ASSERT_EQ(ENOSYS, errno, "errno incorrect");
#else
  ASSERT_EQ(EINVAL, errno, "errno incorrect");
#endif
}
