// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include "filesystems.h"
#include "misc.h"

bool test_append(void) {
    BEGIN_TEST;

    char buf[4096];
    const char* hello = "Hello, ";
    const char* world = "World!\n";
    struct stat st;

    int fd = open("::alpha", O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0);

    // Write "hello"
    ASSERT_EQ(strlen(hello), strlen(world));
    ASSERT_STREAM_ALL(write, fd, hello, strlen(hello));
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(read, fd, buf, strlen(hello));
    ASSERT_EQ(strncmp(buf, hello, strlen(hello)), 0);

    // At the start of the file, write "world"
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(write, fd, world, strlen(world));
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(read, fd, buf, strlen(world));

    // Ensure that the file contains "world", but not "hello"
    ASSERT_EQ(strncmp(buf, world, strlen(world)), 0);
    ASSERT_EQ(stat("::alpha", &st), 0);
    ASSERT_EQ(st.st_size, (off_t)strlen(world));
    ASSERT_EQ(unlink("::alpha"), 0);
    ASSERT_EQ(close(fd), 0);

    fd = open("::alpha", O_RDWR | O_CREAT | O_APPEND, 0644);
    ASSERT_GT(fd, 0);

    // Write "hello"
    ASSERT_EQ(strlen(hello), strlen(world));
    ASSERT_STREAM_ALL(write, fd, hello, strlen(hello));
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(read, fd, buf, strlen(hello));
    ASSERT_EQ(strncmp(buf, hello, strlen(hello)), 0);

    // At the start of the file, write "world"
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(write, fd, world, strlen(world));
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(read, fd, buf, strlen(hello) + strlen(world));

    // Ensure that the file contains both "hello" and "world"
    ASSERT_EQ(strncmp(buf, hello, strlen(hello)), 0);
    ASSERT_EQ(strncmp(buf + strlen(hello), world, strlen(world)), 0);
    ASSERT_EQ(stat("::alpha", &st), 0);
    ASSERT_EQ(st.st_size, (off_t)(strlen(hello) + strlen(world)));
    ASSERT_EQ(unlink("::alpha"), 0);
    ASSERT_EQ(close(fd), 0);

    END_TEST;
}

template <size_t kNumThreads>
bool test_append_atomic(void) {
    BEGIN_TEST;

    constexpr size_t kWriteLength = 32;
    constexpr size_t kNumWrites = 128;

    // Create a group of threads which all append 'i' to a file.
    // At the end of this test, we should see:
    // - A file of length kWriteLength * kNumWrites * kNumThreads.
    // - kWriteLength * kNumWrites of the character 'i' for all
    // values of i in the range [0, kNumThreads).
    // - Those 'i's should be grouped in units of kWriteLength.
    thrd_t threads[kNumThreads];
    for (size_t i = 0; i < kNumThreads; i++) {
        ASSERT_EQ(thrd_create(&threads[i], [](void* arg) {
            size_t i = reinterpret_cast<size_t>(arg);
            int fd = open("::append-atomic", O_WRONLY | O_CREAT | O_APPEND);
            if (fd < 0) {
                return -1;
            }

            char buf[kWriteLength];
            memset(buf, static_cast<int>(i), sizeof(buf));

            for (size_t j = 0; j < kNumWrites; j++) {
                if (write(fd, buf, sizeof(buf)) != sizeof(buf)) {
                    return -1;
                }
            }

            return close(fd);
        }, reinterpret_cast<void*>(i)), thrd_success);
    }

    for (size_t i = 0; i < kNumThreads; i++) {
        int rc;
        ASSERT_EQ(thrd_join(threads[i], &rc), thrd_success);
        ASSERT_EQ(rc, 0);
    }

    // Verify the contents of the file
    int fd = open("::append-atomic", O_RDONLY);
    ASSERT_GT(fd, 0, "Can't reopen file for verification");
    struct stat st;
    ASSERT_EQ(fstat(fd, &st), 0);
    ASSERT_EQ(st.st_size, kWriteLength * kNumWrites * kNumThreads);

    char buf[kWriteLength * kNumWrites * kNumThreads];
    ASSERT_EQ(read(fd, buf, sizeof(buf)), sizeof(buf));

    size_t counts[kNumThreads]{};
    for (size_t i = 0; i < sizeof(buf); i += kWriteLength) {
        size_t val = static_cast<size_t>(buf[i]);
        ASSERT_LE(val, sizeof(counts), "Read unexpected value from file");
        counts[val]++;
        char tmp[kWriteLength];
        memset(tmp, buf[i], sizeof(tmp));

        ASSERT_EQ(memcmp(&buf[i], tmp, sizeof(tmp)), 0, "Non-atomic Append Detected");
    }

    for (size_t i = 0; i < countof(counts); i++) {
        ASSERT_EQ(counts[i], kNumWrites, "Unexpected number of writes from a thread");
    }

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(unlink("::append-atomic"), 0);
    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(append_tests,
    RUN_TEST_MEDIUM(test_append)
    RUN_TEST_MEDIUM((test_append_atomic<1>))
    RUN_TEST_MEDIUM((test_append_atomic<2>))
    RUN_TEST_MEDIUM((test_append_atomic<5>))
    RUN_TEST_MEDIUM((test_append_atomic<10>))
)
