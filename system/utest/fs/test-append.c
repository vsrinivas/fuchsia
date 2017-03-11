// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "filesystems.h"

bool test_append(void) {
    BEGIN_TEST;

    char buf[4096];
    const char* hello = "Hello, ";
    const char* world = "World!\n";
    struct stat st;

    int fd = open("::alpha", O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0, "");

    // Write "hello"
    ASSERT_EQ(strlen(hello), strlen(world), "");
    ASSERT_STREAM_ALL(write, fd, hello, strlen(hello));
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_STREAM_ALL(read, fd, buf, strlen(hello));
    ASSERT_EQ(strncmp(buf, hello, strlen(hello)), 0, "");

    // At the start of the file, write "world"
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_STREAM_ALL(write, fd, world, strlen(world));
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_STREAM_ALL(read, fd, buf, strlen(world));

    // Ensure that the file contains "world", but not "hello"
    ASSERT_EQ(strncmp(buf, world, strlen(world)), 0, "");
    ASSERT_EQ(stat("::alpha", &st), 0, "");
    ASSERT_EQ(st.st_size, (off_t)strlen(world), "");
    ASSERT_EQ(unlink("::alpha"), 0, "");
    ASSERT_EQ(close(fd), 0, "");

    fd = open("::alpha", O_RDWR | O_CREAT | O_APPEND, 0644);
    ASSERT_GT(fd, 0, "");

    // Write "hello"
    ASSERT_EQ(strlen(hello), strlen(world), "");
    ASSERT_STREAM_ALL(write, fd, hello, strlen(hello));
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_STREAM_ALL(read, fd, buf, strlen(hello));
    ASSERT_EQ(strncmp(buf, hello, strlen(hello)), 0, "");

    // At the start of the file, write "world"
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_STREAM_ALL(write, fd, world, strlen(world));
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_STREAM_ALL(read, fd, buf, strlen(hello) + strlen(world));

    // Ensure that the file contains both "hello" and "world"
    ASSERT_EQ(strncmp(buf, hello, strlen(hello)), 0, "");
    ASSERT_EQ(strncmp(buf + strlen(hello), world, strlen(world)), 0, "");
    ASSERT_EQ(stat("::alpha", &st), 0, "");
    ASSERT_EQ(st.st_size, (off_t)(strlen(hello) + strlen(world)), "");
    ASSERT_EQ(unlink("::alpha"), 0, "");
    ASSERT_EQ(close(fd), 0, "");

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(append_tests,
    RUN_TEST_MEDIUM(test_append)
)
