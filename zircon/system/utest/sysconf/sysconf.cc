// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <unistd.h>
#include <zxtest/zxtest.h>

namespace {

TEST(SysconfTest, NProcessors) {
    long rv;
    rv = sysconf(_SC_NPROCESSORS_CONF);
    EXPECT_GE(rv, 1, "wrong number of cpus configured");
    rv = sysconf(_SC_NPROCESSORS_ONLN);
    EXPECT_GE(rv, 1, "wrong number of cpus currently online");
}

TEST(SysconfTest, InvalidInput) {
    // test on invalid input
    errno = 0;
    long rv = sysconf(-1);
    EXPECT_EQ(rv, -1, "wrong return value on invalid input");
    EXPECT_EQ(errno, EINVAL, "wrong errno value on invalid input");
}

TEST(SysconfTest, IndeterminateLimit) {
    // Indeterminate limit.
    errno = 0;
    long rv = sysconf(_SC_ARG_MAX);
    EXPECT_EQ(rv, -1, "wrong return value for indeterminate limit {ARG_MAX}");
    EXPECT_EQ(errno, 0, "wrong errno value for indeterminate limit {ARG_MAX}");
}

} // anonymous namespace
