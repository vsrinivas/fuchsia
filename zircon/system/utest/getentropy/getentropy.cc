// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <sys/random.h>

#include <zxtest/zxtest.h>

TEST(GetentropyTests, getentropy_valid) {
  char buf[16];

  errno = 0;
  int result = getentropy(buf, sizeof(buf));
  int err = errno;

  EXPECT_EQ(result, 0);
  EXPECT_EQ(err, 0);
}

TEST(GetentropyTests, getentropy_too_big) {
  const size_t size = 1024 * 1024 * 1024;

  char* buf = static_cast<char*>(malloc(size));
  EXPECT_NOT_NULL(buf);

  errno = 0;
  int result = getentropy(buf, size);
  int err = errno;

  EXPECT_EQ(result, -1);
  EXPECT_EQ(err, EIO);

  free(buf);
}
