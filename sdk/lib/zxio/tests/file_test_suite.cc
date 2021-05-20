// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_test_suite.h"

#include <string.h>

#include <zxtest/zxtest.h>

namespace FileTestSuite {

void ReadWrite(zxio_t* io) {
  size_t actual = 0u;
  ASSERT_OK(zxio_write(io, "abcd", 4, 0, &actual));
  EXPECT_EQ(actual, 4u);

  size_t seek = 0;
  ASSERT_OK(zxio_seek(io, ZXIO_SEEK_ORIGIN_CURRENT, -2, &seek));
  EXPECT_EQ(2u, seek);

  char buffer[1024] = {};
  actual = 0u;
  ASSERT_OK(zxio_read(io, buffer, 1024, 0, &actual));
  EXPECT_EQ(actual, 2u);
  EXPECT_STR_EQ("cd", buffer);
  memset(buffer, 0, sizeof(buffer));

  actual = 2;
  ASSERT_OK(zxio_write_at(io, 1, "xy", 2, 0, &actual));
  EXPECT_EQ(actual, 2u);

  actual = 0u;
  ASSERT_OK(zxio_read_at(io, 1, buffer, 1024, 0, &actual));
  EXPECT_EQ(actual, 3u);
  EXPECT_STR_EQ("xyd", buffer);
  memset(buffer, 0, sizeof(buffer));
}

}  // namespace FileTestSuite
