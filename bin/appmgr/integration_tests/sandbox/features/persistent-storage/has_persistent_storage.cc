// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "gtest/gtest.h"

TEST(FeaturesTest, HasPersistentStorage) {
  struct stat stat_;
  // /data is there
  int retval = stat("/data", &stat_);
  ASSERT_EQ(retval, 0) << "Can't find /data: " << strerror(errno);
  ASSERT_TRUE(S_ISDIR(stat_.st_mode)) << "/data is not a directory";
  // Unlike a path that doesn't exist
  retval = stat("/this_should_not_exist", &stat_);
  ASSERT_EQ(retval, -1);
}
