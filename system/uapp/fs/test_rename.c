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
    EXPECT_FAIL(rename("::alpha", "::bravo")); // Cannot rename when src does not exist
    TRY(mkdir("::alpha", 0755));
    EXPECT_FAIL(rename("::alpha", "::alpha")); // Cannot rename to self
    int fd = TRY(open("::bravo", O_RDWR|O_CREAT|O_EXCL, 0644));
    close(fd);
    EXPECT_FAIL(rename("::alpha", "::bravo")); // Cannot rename dir to file
    TRY(unlink("::bravo"));
    TRY(rename("::alpha", "::bravo")); // Rename dir (dst does not exist)
    TRY(mkdir("::alpha", 0755));
    TRY(rename("::bravo", "::alpha")); // Rename dir (dst does exist)
    fd = TRY(open("::alpha/charlie", O_RDWR|O_CREAT|O_EXCL, 0644));
    TRY(rename("::alpha/charlie", "::alpha/delta")); // Rename file (dst does not exist)
    close(fd);
    fd = TRY(open("::alpha/charlie", O_RDWR|O_CREAT|O_EXCL, 0644));
    TRY(rename("::alpha/delta", "::alpha/charlie")); // Rename file (dst does not exist)
    EXPECT_FAIL(rename("::alpha/charlie", "::charlie")); // Cannot rename outside current directory
    close(fd);
    TRY(unlink("::alpha/charlie"));
    TRY(unlink("::alpha"));
    return 0;
}

