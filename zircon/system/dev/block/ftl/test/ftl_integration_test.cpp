// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "ftl_test_observer.h"
#include "launch.h"

// TODO(FLK-160): Re-enable when flakiness is fixed.
#if 0
TEST(FtlTest, BlockTest) {
    const char* argv[] = {"/boot/bin/blktest", "-d", kTestDevice};

    ASSERT_EQ(0, Execute(countof(argv), argv));
}

TEST(FtlTest, IoCheck) {
    const char* argv[] = {
        "/boot/bin/iochk",
        "-bs", "32k",
        "--live-dangerously",
        "-t", "2",
        kTestDevice
    };

    ASSERT_EQ(0, Execute(countof(argv), argv));
}
#endif
