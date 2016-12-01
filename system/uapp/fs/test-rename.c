// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "misc.h"

int test_rename(void) {
    // Cannot rename when src does not exist
    EXPECT_FAIL(rename("::alpha", "::bravo"));

    // Cannot rename to self
    TRY(mkdir("::alpha", 0755));
    EXPECT_FAIL(rename("::alpha", "::alpha"));

    // Cannot rename dir to file
    int fd = TRY(open("::bravo", O_RDWR|O_CREAT|O_EXCL, 0644));
    close(fd);
    EXPECT_FAIL(rename("::alpha", "::bravo"));
    TRY(unlink("::bravo"));

    // Rename dir (dst does not exist)
    TRY(rename("::alpha", "::bravo"));
    TRY(mkdir("::alpha", 0755));
    // Rename dir (dst does exist)
    TRY(rename("::bravo", "::alpha"));

    // Rename file (dst does not exist)
    fd = TRY(open("::alpha/charlie", O_RDWR|O_CREAT|O_EXCL, 0644));
    TRY(rename("::alpha/charlie", "::alpha/delta"));
    close(fd);

    // Rename file (dst does not exist)
    fd = TRY(open("::alpha/charlie", O_RDWR|O_CREAT|O_EXCL, 0644));
    TRY(rename("::alpha/delta", "::alpha/charlie"));
    close(fd);

    // Rename to different directory
    TRY(mkdir("::bravo", 0755));
    TRY(rename("::alpha/charlie", "::charlie"));
    TRY(rename("::charlie", "::alpha/charlie"));
    TRY(rename("::bravo", "::alpha/bravo"));
    TRY(rename("::alpha/charlie", "::alpha/bravo/charlie"));

    // Cannot rename directory to subdirectory of itself
    EXPECT_FAIL(rename("::alpha", "::alpha/bravo"));
    EXPECT_FAIL(rename("::alpha", "::alpha/bravo/charlie"));
    EXPECT_FAIL(rename("::alpha", "::alpha/bravo/charlie/delta"));
    EXPECT_FAIL(rename("::alpha", "::alpha/delta"));
    EXPECT_FAIL(rename("::alpha/bravo", "::alpha/bravo/charlie"));
    EXPECT_FAIL(rename("::alpha/bravo", "::alpha/bravo/charlie/delta"));
    // Cannot rename to non-empty directory
    EXPECT_FAIL(rename("::alpha/bravo/charlie", "::alpha/bravo"));
    EXPECT_FAIL(rename("::alpha/bravo/charlie", "::alpha"));
    EXPECT_FAIL(rename("::alpha/bravo", "::alpha"));

    // Clean up
    TRY(unlink("::alpha/bravo/charlie"));
    TRY(unlink("::alpha/bravo"));
    TRY(unlink("::alpha"));
    return 0;
}

