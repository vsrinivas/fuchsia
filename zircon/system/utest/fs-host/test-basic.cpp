// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

bool test_basic(void) {
    BEGIN_TEST;
    ASSERT_EQ(emu_mkdir("::alpha", 0755), 0);
    ASSERT_EQ(emu_mkdir("::alpha/bravo", 0755), 0);
    ASSERT_EQ(emu_mkdir("::alpha/bravo/charlie", 0755), 0);
    ASSERT_EQ(emu_mkdir("::alpha/bravo/charlie/delta", 0755), 0);
    ASSERT_EQ(emu_mkdir("::alpha/bravo/charlie/delta/echo", 0755), 0);
    int fd1 = emu_open("::alpha/bravo/charlie/delta/echo/foxtrot", O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd1, 0);
    int fd2 = emu_open("::alpha/bravo/charlie/delta/echo/foxtrot", O_RDWR, 0644);
    ASSERT_GT(fd2, 0);
    ASSERT_EQ(emu_write(fd1, "Hello, World!\n", 14), 14);
    ASSERT_EQ(emu_close(fd1), 0);
    ASSERT_EQ(emu_close(fd2), 0);

    fd1 = emu_open("::file.txt", O_CREAT | O_RDWR, 0644);
    ASSERT_GT(fd1, 0);
    ASSERT_EQ(emu_close(fd1), 0);

    ASSERT_EQ(emu_mkdir("::emptydir", 0755), 0);
    fd1 = emu_open("::emptydir", O_RDONLY, 0644);
    ASSERT_GT(fd1, 0);
    char buf;
    ASSERT_LT(emu_read(fd1, &buf, 1), 0);
    ASSERT_EQ(emu_write(fd1, "Don't write to directories", 26), -1);
    ASSERT_EQ(emu_ftruncate(fd1, 0), -1);
    ASSERT_EQ(emu_close(fd1), 0);
    ASSERT_EQ(run_fsck(), 0);
    END_TEST;
}

RUN_MINFS_TESTS(basic_tests,
    RUN_TEST_MEDIUM(test_basic)
)