// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <unistd.h>

#include "gtest/gtest.h"

TEST(FeaturesTest, HasPersistentStorage) {
  struct stat dontcare;
  // /data is there
  int retval = stat("/data", &dontcare);
  ASSERT_EQ(retval, 0) << "errno: " << errno << std::endl;
  // Unlike a path that doesn't exist
  retval = stat("/this_should_not_exist", &dontcare);
  ASSERT_EQ(retval, -1);
}
