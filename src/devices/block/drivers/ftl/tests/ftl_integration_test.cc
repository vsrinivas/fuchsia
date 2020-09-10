// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "ftl_test_observer.h"
#include "launch.h"

TEST(FtlTest, BlockTest) {
  const char* argv[] = {"/pkg/bin/blktest", "-d", kTestDevice, nullptr};

  ASSERT_EQ(0, Execute(argv));
}

TEST(FtlTest, IoCheck) {
  const char* argv[] = {"/pkg/bin/iochk", "-bs",  "32k", "--live-dangerously", "-t", "2",
                        kTestDevice,      nullptr};

  ASSERT_EQ(0, Execute(argv));
}
