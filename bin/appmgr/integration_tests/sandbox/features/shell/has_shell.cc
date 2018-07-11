// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "gtest/gtest.h"

TEST(FeaturesTest, HasShell) {
  struct stat stat_;
  // Some directories that only shell can access are present
  for (auto dir : {"/boot", "/system", "/hub", "/pkgfs", "/config/ssl"}) {
    int retval = stat(dir, &stat_);
    ASSERT_EQ(retval, 0) << "Can't find " << dir << ": " << strerror(errno);
    ASSERT_TRUE(S_ISDIR(stat_.st_mode)) << dir << " is not a directory";
  }

  // Unlike a path that doesn't exist
  int retval = stat("/this_should_not_exist", &stat_);
  ASSERT_EQ(retval, -1);
}
