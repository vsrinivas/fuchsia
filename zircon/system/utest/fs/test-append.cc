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

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>

#include "filesystems.h"
#include "misc.h"

namespace {

bool test_append() {
    BEGIN_TEST;

    char buf[4096];
    const char* hello = "Hello, ";
    const char* world = "World!\n";
    struct stat st;

    fbl::unique_fd fd(open("::alpha", O_RDWR | O_CREAT, 0644));
    ASSERT_TRUE(fd);

    // Write "hello"
    ASSERT_EQ(strlen(hello), strlen(world));
    ASSERT_STREAM_ALL(write, fd.get(), hello, strlen(hello));
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(read, fd.get(), buf, strlen(hello));
    ASSERT_EQ(strncmp(buf, hello, strlen(hello)), 0);

    // At the start of the file, write "world"
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(write, fd.get(), world, strlen(world));
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(read, fd.get(), buf, strlen(world));

    // Ensure that the file contains "world", but not "hello"
    ASSERT_EQ(strncmp(buf, world, strlen(world)), 0);
    ASSERT_EQ(stat("::alpha", &st), 0);
    ASSERT_EQ(st.st_size, (off_t)strlen(world));
    ASSERT_EQ(unlink("::alpha"), 0);
    ASSERT_EQ(close(fd.release()), 0);

    fd.reset(open("::alpha", O_RDWR | O_CREAT | O_APPEND, 0644));
    ASSERT_TRUE(fd);

    // Write "hello"
    ASSERT_EQ(strlen(hello), strlen(world));
    ASSERT_STREAM_ALL(write, fd.get(), hello, strlen(hello));
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(read, fd.get(), buf, strlen(hello));
    ASSERT_EQ(strncmp(buf, hello, strlen(hello)), 0);

    // At the start of the file, write "world"
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(write, fd.get(), world, strlen(world));
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(read, fd.get(), buf, strlen(hello) + strlen(world));

    // Ensure that the file contains both "hello" and "world"
    ASSERT_EQ(strncmp(buf, hello, strlen(hello)), 0);
    ASSERT_EQ(strncmp(buf + strlen(hello), world, strlen(world)), 0);
    ASSERT_EQ(stat("::alpha", &st), 0);
    ASSERT_EQ(st.st_size, (off_t)(strlen(hello) + strlen(world)));
    ASSERT_EQ(unlink("::alpha"), 0);
    ASSERT_EQ(close(fd.release()), 0);

    END_TEST;
}

bool TestAppendOnClone() {
    BEGIN_TEST;

    enum AppendState {
        Append,
        NoAppend,
    };

    auto verify_append = [](fbl::unique_fd& fd, AppendState appendState) {
        BEGIN_HELPER;

        // Ensure we have a file of non-zero size.
        char buf[32];
        memset(buf, 'a', sizeof(buf));
        ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
        ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), sizeof(buf));
        struct stat st;
        ASSERT_EQ(fstat(fd.get(), &st), 0);
        off_t size = st.st_size;

        // Write at the 'start' of the file.
        ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
        ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), sizeof(buf));
        ASSERT_EQ(fstat(fd.get(), &st), 0);

        switch (appendState) {
        case Append:
            // Even though we wrote to the 'start' of the file, it
            // appends to the end if the file was opened as O_APPEND.
            ASSERT_EQ(st.st_size, size + static_cast<off_t>(sizeof(buf)));
            ASSERT_EQ(fcntl(fd.get(), F_GETFL), O_APPEND | O_RDWR);
            break;
        case NoAppend:
            // We wrote to the start of the file, so the size
            // should be unchanged.
            ASSERT_EQ(st.st_size, size);
            ASSERT_EQ(fcntl(fd.get(), F_GETFL), O_RDWR);
            break;
        default:
            ASSERT_TRUE(false);
        }
        END_HELPER;
    };

    fbl::unique_fd fd(open("::append_clone", O_RDWR | O_CREAT | O_APPEND));
    ASSERT_TRUE(fd);
    // Verify the file was originally opened as append.
    ASSERT_TRUE(verify_append(fd, Append));

    // Verify we can toggle append off and back on.
    ASSERT_EQ(fcntl(fd.get(), F_SETFL, 0), 0);
    ASSERT_TRUE(verify_append(fd, NoAppend));
    ASSERT_EQ(fcntl(fd.get(), F_SETFL, O_APPEND), 0);
    ASSERT_TRUE(verify_append(fd, Append));

    // Verify that cloning the fd doesn't lose the APPEND flag.
    zx_handle_t handle = ZX_HANDLE_INVALID;
    ASSERT_EQ(ZX_OK, fdio_fd_clone(fd.get(), &handle), "fdio_fd_clone failed");

    int raw_fd = -1;
    ASSERT_EQ(fdio_fd_create(handle, &raw_fd), ZX_OK);
    fbl::unique_fd cloned_fd(raw_fd);
    ASSERT_TRUE(verify_append(cloned_fd, Append));

    ASSERT_EQ(unlink("::append_clone"), 0);
    END_TEST;
}

template <size_t kNumThreads>
bool TestAppendAtomic() {
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
            fbl::unique_fd fd(open("::append-atomic", O_WRONLY | O_CREAT | O_APPEND));
            if (!fd) {
                return -1;
            }

            char buf[kWriteLength];
            memset(buf, static_cast<int>(i), sizeof(buf));

            for (size_t j = 0; j < kNumWrites; j++) {
                if (write(fd.get(), buf, sizeof(buf)) != sizeof(buf)) {
                    return -1;
                }
            }

            return close(fd.release());
        }, reinterpret_cast<void*>(i)), thrd_success);
    }

    for (size_t i = 0; i < kNumThreads; i++) {
        int rc;
        ASSERT_EQ(thrd_join(threads[i], &rc), thrd_success);
        ASSERT_EQ(rc, 0);
    }

    // Verify the contents of the file
    fbl::unique_fd fd(open("::append-atomic", O_RDONLY));
    ASSERT_GT(fd.get(), 0, "Can't reopen file for verification");
    struct stat st;
    ASSERT_EQ(fstat(fd.get(), &st), 0);
    ASSERT_EQ(st.st_size, kWriteLength * kNumWrites * kNumThreads);

    char buf[kWriteLength * kNumWrites * kNumThreads];
    ASSERT_EQ(read(fd.get(), buf, sizeof(buf)), sizeof(buf));

    size_t counts[kNumThreads]{};
    for (size_t i = 0; i < sizeof(buf); i += kWriteLength) {
        size_t val = static_cast<size_t>(buf[i]);
        ASSERT_LE(val, sizeof(counts), "Read unexpected value from file");
        counts[val]++;
        char tmp[kWriteLength];
        memset(tmp, buf[i], sizeof(tmp));

        ASSERT_EQ(memcmp(&buf[i], tmp, sizeof(tmp)), 0, "Non-atomic Append Detected");
    }

    for (size_t i = 0; i < fbl::count_of(counts); i++) {
        ASSERT_EQ(counts[i], kNumWrites, "Unexpected number of writes from a thread");
    }

    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_EQ(unlink("::append-atomic"), 0);
    END_TEST;
}

}

RUN_FOR_ALL_FILESYSTEMS(append_tests,
    RUN_TEST_MEDIUM(test_append)
    RUN_TEST_MEDIUM(TestAppendOnClone)
    RUN_TEST_MEDIUM((TestAppendAtomic<1>))
    RUN_TEST_MEDIUM((TestAppendAtomic<2>))
    RUN_TEST_MEDIUM((TestAppendAtomic<5>))
    RUN_TEST_MEDIUM((TestAppendAtomic<10>))
)
