// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <unittest/unittest.h>

#include "filesystems.h"

bool test_basic(void) {
    BEGIN_TEST;

    ASSERT_EQ(mkdir("::alpha", 0755), 0, "");
    ASSERT_EQ(mkdir("::alpha/bravo", 0755), 0, "");
    ASSERT_EQ(mkdir("::alpha/bravo/charlie", 0755), 0, "");
    ASSERT_EQ(mkdir("::alpha/bravo/charlie/delta", 0755), 0, "");
    ASSERT_EQ(mkdir("::alpha/bravo/charlie/delta/echo", 0755), 0, "");
    int fd1 = open("::alpha/bravo/charlie/delta/echo/foxtrot", O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd1, 0, "");
    int fd2 = open("::alpha/bravo/charlie/delta/echo/foxtrot", O_RDWR, 0644);
    ASSERT_GT(fd2, 0, "");
    ASSERT_EQ(write(fd1, "Hello, World!\n", 14), 14, "");
    ASSERT_EQ(close(fd1), 0, "");
    ASSERT_EQ(close(fd2), 0, "");

    fd1 = open("::file.txt", O_CREAT | O_RDWR, 0644);
    ASSERT_GT(fd1, 0, "");
    ASSERT_EQ(close(fd1), 0, "");

    ASSERT_EQ(unlink("::file.txt"), 0, "");
    ASSERT_EQ(mkdir("::emptydir", 0755), 0, "");
    fd1 = open("::emptydir", O_RDONLY, 0644);
    ASSERT_GT(fd1, 0, "");

    ASSERT_EQ(read(fd1, NULL, 0), -1, "");
    ASSERT_EQ(write(fd1, "Don't write to directories", 26), -1, "");
    ASSERT_EQ(ftruncate(fd1, 0), -1, "");
    ASSERT_EQ(rmdir("::emptydir"), 0, "");
    ASSERT_EQ(rmdir("::emptydir"), -1, "");
    ASSERT_EQ(close(fd1), 0, "");
    ASSERT_EQ(rmdir("::emptydir"), -1, "");

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(basic_tests,
    RUN_TEST_MEDIUM(test_basic)
)
