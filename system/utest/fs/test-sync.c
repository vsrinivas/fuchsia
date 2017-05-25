// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "filesystems.h"
#include "misc.h"

// TODO(smklein): Create a more complex test, capable of mocking a block device
// and ensuring that data is actually being flushed to a block device.
// For now, test that 'fsync' and 'fdatasync' don't throw errors for file and
// directories.
bool test_sync(void) {
    BEGIN_TEST;

    int fd = open("::alpha", O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_STREAM_ALL(write, fd, "Hello, World!\n", 14);
    ASSERT_EQ(fsync(fd), 0, "");
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_STREAM_ALL(write, fd, "Adios, World!\n", 14);
    ASSERT_EQ(fdatasync(fd), 0, "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(unlink("::alpha"), 0, "");

    ASSERT_EQ(mkdir("::dirname", 0755), 0, "");
    fd = open("::dirname", O_RDONLY | O_DIRECTORY, 0644);
    ASSERT_GT(fd, 0, "");
    ASSERT_EQ(fsync(fd), 0, "");
    ASSERT_EQ(fdatasync(fd), 0, "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(unlink("::dirname"), 0, "");

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(sync_tests,
    RUN_TEST_MEDIUM(test_sync)
)
