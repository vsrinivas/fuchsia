// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "gtest/gtest.h"

TEST(FeaturesTest, NoShell) {
  struct stat stat_;
  // Can't access directories that only shell can access
  for (auto dir : {"/boot", "/system", "/hub", "/pkgfs"}) {
    int retval = stat(dir, &stat_);
    ASSERT_EQ(retval, -1) << "Unexpectedly found " << dir;
  }

  // While /svc is present
  int retval = stat("/svc", &stat_);
  ASSERT_EQ(retval, 0) << "Can't find /svc: " << strerror(errno);
}
