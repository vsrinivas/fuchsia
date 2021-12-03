// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fit/defer.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

TEST(RmdirTest, Rmdir) {
  char filename[] = "/tmp/fdio-rmdir-file.XXXXXX";
  ASSERT_TRUE(fbl::unique_fd(mkstemp(filename)), "%s", strerror(errno));
  auto cleanup =
      fit::defer([&filename]() { EXPECT_EQ(unlink(filename), 0, "%s", strerror(errno)); });
  EXPECT_EQ(rmdir(filename), -1);
  EXPECT_EQ(errno, ENOTDIR);

  char dirname[] = "/tmp/fdio-rmdir-dir.XXXXXX";
  ASSERT_NOT_NULL(mkdtemp(dirname), "%s", strerror(errno));
  EXPECT_EQ(rmdir(dirname), 0);
}

}  // namespace
