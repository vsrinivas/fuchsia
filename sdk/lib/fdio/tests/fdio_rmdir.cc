// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

TEST(RmdirTest, Rmdir) {
  const char* filename = "/tmp/foo";
  fbl::unique_fd foo_fd(open(filename, O_CREAT | O_RDWR, 0644));
  EXPECT_EQ(rmdir(filename), -1);
  EXPECT_EQ(errno, ENOTDIR);

  const char* dirname = "/tmp/baz";
  EXPECT_EQ(mkdir(dirname, 0777), 0);
  EXPECT_EQ(rmdir(dirname), 0);
}

}  // namespace
