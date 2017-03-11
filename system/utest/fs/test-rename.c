// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "filesystems.h"

int test_rename(void) {
    BEGIN_TEST;
    // Cannot rename when src does not exist
    ASSERT_EQ(rename("::alpha", "::bravo"), -1, "");

    // Cannot rename to self
    ASSERT_EQ(mkdir("::alpha", 0755), 0, "");
    ASSERT_EQ(rename("::alpha", "::alpha"), -1, "");

    // Cannot rename dir to file
    int fd = open("::bravo", O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(rename("::alpha", "::bravo"), -1, "");
    ASSERT_EQ(unlink("::bravo"), 0, "");

    // Rename dir (dst does not exist)
    ASSERT_EQ(rename("::alpha", "::bravo"), 0, "");
    ASSERT_EQ(mkdir("::alpha", 0755), 0, "");
    // Rename dir (dst does exist)
    ASSERT_EQ(rename("::bravo", "::alpha"), 0, "");

    // Rename file (dst does not exist)
    fd = open("::alpha/charlie", O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(rename("::alpha/charlie", "::alpha/delta"), 0, "");
    ASSERT_EQ(close(fd), 0, "");

    // Rename file (dst does not exist)
    fd = open("::alpha/charlie", O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(rename("::alpha/delta", "::alpha/charlie"), 0, "");
    ASSERT_EQ(close(fd), 0, "");

    // Rename to different directory
    ASSERT_EQ(mkdir("::bravo", 0755), 0, "");
    ASSERT_EQ(rename("::alpha/charlie", "::charlie"), 0, "");
    ASSERT_EQ(rename("::charlie", "::alpha/charlie"), 0, "");
    ASSERT_EQ(rename("::bravo", "::alpha/bravo"), 0, "");
    ASSERT_EQ(rename("::alpha/charlie", "::alpha/bravo/charlie"), 0, "");

    // Cannot rename directory to subdirectory of itself
    ASSERT_EQ(rename("::alpha", "::alpha/bravo"), -1, "");
    ASSERT_EQ(rename("::alpha", "::alpha/bravo/charlie"), -1, "");
    ASSERT_EQ(rename("::alpha", "::alpha/bravo/charlie/delta"), -1, "");
    ASSERT_EQ(rename("::alpha", "::alpha/delta"), -1, "");
    ASSERT_EQ(rename("::alpha/bravo", "::alpha/bravo/charlie"), -1, "");
    ASSERT_EQ(rename("::alpha/bravo", "::alpha/bravo/charlie/delta"), -1, "");
    // Cannot rename to non-empty directory
    ASSERT_EQ(rename("::alpha/bravo/charlie", "::alpha/bravo"), -1, "");
    ASSERT_EQ(rename("::alpha/bravo/charlie", "::alpha"), -1, "");
    ASSERT_EQ(rename("::alpha/bravo", "::alpha"), -1, "");

    // Clean up
    ASSERT_EQ(unlink("::alpha/bravo/charlie"), 0, "");
    ASSERT_EQ(unlink("::alpha/bravo"), 0, "");
    ASSERT_EQ(unlink("::alpha"), 0, "");

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(rename_tests,
    RUN_TEST_MEDIUM(test_rename)
)
