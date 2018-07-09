// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <unistd.h>

#include "gtest/gtest.h"

TEST(FeaturesTest, NoPersistentStorage) {
  struct stat dontcare;
  // /data is missing
  int retval = stat("/data", &dontcare);
  ASSERT_EQ(retval, -1);
  // While /svc is present
  retval = stat("/svc", &dontcare);
  ASSERT_EQ(retval, 0) << "errno: " << errno << std::endl;
}
