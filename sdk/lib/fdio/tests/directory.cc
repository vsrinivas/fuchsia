// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/socket.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

TEST(DirectoryTest, UnsupportedOps) {
  fbl::unique_fd fd(open("/", O_DIRECTORY | O_RDONLY));
  ASSERT_TRUE(fd.is_valid());

  ASSERT_EQ(bind(fd.get(), nullptr, 0), -1);
  ASSERT_ERRNO(ENOTSOCK);
  ASSERT_EQ(connect(fd.get(), nullptr, 0), -1);
  ASSERT_ERRNO(ENOTSOCK);
  ASSERT_EQ(listen(fd.get(), 0), -1);
  ASSERT_ERRNO(ENOTSOCK);
  ASSERT_EQ(accept(fd.get(), nullptr, nullptr), -1);
  ASSERT_ERRNO(ENOTSOCK);
  ASSERT_EQ(getsockname(fd.get(), nullptr, nullptr), -1);
  ASSERT_ERRNO(ENOTSOCK);
  ASSERT_EQ(getpeername(fd.get(), nullptr, nullptr), -1);
  ASSERT_ERRNO(ENOTSOCK);
  ASSERT_EQ(getsockopt(fd.get(), 0, 0, nullptr, nullptr), -1);
  ASSERT_ERRNO(ENOTSOCK);
  ASSERT_EQ(setsockopt(fd.get(), 0, 0, nullptr, 0), -1);
  ASSERT_ERRNO(ENOTSOCK);
  ASSERT_EQ(shutdown(fd.get(), 0), -1);
  ASSERT_ERRNO(ENOTSOCK);
}
