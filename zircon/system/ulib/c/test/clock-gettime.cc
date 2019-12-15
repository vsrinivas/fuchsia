// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <string.h>
#include <time.h>

#include <zxtest/zxtest.h>

TEST(PosixClockTests, BootTimeIsMonotonicTime) {
  // The test strategy here is limited, as we do not have a straightforward
  // mechanism with which to modify the underlying syscall behavior. We switch
  // back and forward between calling clock_gettime with CLOCK_MONOTONIC and
  // CLOCK_BOOTTIME, and assert their relative monotonicity. This test ensures
  // that these calls succeed, and that time is at least frozen, if not
  // increasing in a monotonic fashion, with repect to both clock ids.

  timespec last{};  // Zero is before the first sample.

  int which = 0;
  for (int i = 0; i < 100; i++) {
    timespec ts;

    switch (which) {
      case 0:
        ASSERT_EQ(0, clock_gettime(CLOCK_MONOTONIC, &ts), "%s", strerror(errno));
        break;
      case 1:
        ASSERT_EQ(0, clock_gettime(CLOCK_BOOTTIME, &ts), "%s", strerror(errno));
        break;
      case 2:
        ASSERT_EQ(0, clock_gettime(CLOCK_MONOTONIC_RAW, &ts), "%s", strerror(errno));
        break;
    }

    if (ts.tv_sec == last.tv_sec) {
      EXPECT_GE(ts.tv_nsec, last.tv_nsec, "clock_gettime(CLOCK_{MONOTONIC,BOOTTIME})");
    } else {
      EXPECT_GE(ts.tv_sec, last.tv_sec, "clock_gettime(CLOCK_{MONOTONIC,BOOTTIME})");
    }

    if (++which % 3 == 0) {
      which = 0;
    }

    last = ts;
  }
}
