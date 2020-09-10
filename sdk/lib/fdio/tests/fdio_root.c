// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <lib/fdio/private.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <zxtest/zxtest.h>

// These tests poke at some "global" behavior of fdio
// that are not easily tested through filesystem tests,
// since they (for example) rely on a global root.
//
// For more comprehensive filesystem tests, refer
// to utest/fs.

TEST(RootTest, Stat) {
  struct stat buf;
  ASSERT_EQ(stat("/", &buf), 0, "");
  ASSERT_EQ(stat("//", &buf), 0, "");
  ASSERT_EQ(stat("///", &buf), 0, "");
  ASSERT_EQ(stat("/tmp", &buf), 0, "");
  ASSERT_EQ(stat("//tmp", &buf), 0, "");
  ASSERT_EQ(stat("./", &buf), 0, "");
  ASSERT_EQ(stat("./", &buf), 0, "");
  ASSERT_EQ(stat(".", &buf), 0, "");
}

TEST(RootTest, Remove) {
  ASSERT_EQ(remove("/"), -1, "");
  ASSERT_EQ(errno, EBUSY, "");

  ASSERT_EQ(rmdir("/"), -1, "");
  ASSERT_EQ(errno, EBUSY, "");
}
