// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <mxio/private.h>

#include <unittest/unittest.h>

// These tests poke at some "global" behavior of mxio
// that are not easily tested through filesystem tests,
// since they (for example) rely on a global root.
//
// For more comprehensive filesystem tests, refer
// to utest/fs.

bool stat_test(void) {
    BEGIN_TEST;

    struct stat buf;
    ASSERT_EQ(stat("/", &buf), 0, "");
    ASSERT_EQ(stat("//", &buf), 0, "");
    ASSERT_EQ(stat("///", &buf), 0, "");
    ASSERT_EQ(stat("/tmp", &buf), 0, "");
    ASSERT_EQ(stat("//tmp", &buf), 0, "");
    ASSERT_EQ(stat("./", &buf), 0, "");
    ASSERT_EQ(stat("./", &buf), 0, "");
    ASSERT_EQ(stat(".", &buf), 0, "");

    END_TEST;
}

bool remove_test(void) {
    BEGIN_TEST;

    ASSERT_EQ(remove("/"), -1, "");
    ASSERT_EQ(errno, EBUSY, "");

    ASSERT_EQ(rmdir("/"), -1, "");
    ASSERT_EQ(errno, EBUSY, "");

    END_TEST;
}

BEGIN_TEST_CASE(mxio_root_test)
RUN_TEST(stat_test)
RUN_TEST(remove_test)
END_TEST_CASE(mxio_root_test)
