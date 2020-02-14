// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "ftl_test_observer.h"
#include "launch.h"

// TODO(FLK-160): Re-enable when flakiness is fixed.
#if !defined(__arm__) && !defined(__aarch64__)
TEST(FtlTest, BlockTest) {
  const char* argv[] = {"/boot/bin/blktest", "-d", kTestDevice, nullptr};

  ASSERT_EQ(0, Execute(argv));
}

TEST(FtlTest, IoCheck) {
  const char* argv[] = {"/boot/bin/iochk", "-bs",  "32k", "--live-dangerously", "-t", "2",
                        kTestDevice,       nullptr};

  ASSERT_EQ(0, Execute(argv));
}
#endif
