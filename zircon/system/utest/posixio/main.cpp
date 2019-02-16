// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <unittest/unittest.h>

static bool stat_empty_test() {
    BEGIN_TEST;

    struct stat s;
    int rc = stat("", &s);
    int err = errno;
    ASSERT_EQ(rc, -1);
    ASSERT_EQ(err, ENOENT);

    END_TEST;
}

static bool lstat_empty_test() {
    BEGIN_TEST;

    struct stat s;
    int rc = lstat("", &s);
    int err = errno;
    ASSERT_EQ(rc, -1);
    ASSERT_EQ(err, ENOENT);

    END_TEST;
}

static bool open_empty_test() {
    BEGIN_TEST;

    const int oflags[] = {
        O_EXEC,
        O_RDONLY,
        O_RDWR,
        O_SEARCH,
        O_WRONLY,
    };

    const int additional_oflags[] = {
        0,
        O_APPEND,
        O_CLOEXEC,
        O_APPEND | O_CLOEXEC,
        O_TRUNC,
        O_APPEND | O_TRUNC,
        O_CLOEXEC | O_TRUNC,
        O_APPEND | O_CLOEXEC | O_TRUNC,
    };

    const mode_t modes[] = {
        0777,
        0644,
        0600,
        0000,
    };

    const int fds[] = {
        0,
        1,
        2,
        AT_FDCWD,
    };

    for (int oflag : oflags) {
        for (int additional_oflag : additional_oflags) {
            int flags = oflag | additional_oflag;
            int rc = open("", flags);
            int err = errno;
            ASSERT_EQ(rc, -1);
            ASSERT_EQ(err, ENOENT);

            for (int fd : fds) {
                int rc = openat(fd, "", flags);
                int err = errno;
                ASSERT_EQ(rc, -1);
                ASSERT_EQ(err, ENOENT);
            }

            for (mode_t mode : modes) {
                rc = open("", flags | O_CREAT, mode);
                err = errno;
                ASSERT_EQ(rc, -1);
                ASSERT_EQ(err, ENOENT);

                for (int fd : fds) {
                    int rc = openat(fd, "", flags | O_CREAT, mode);
                    int err = errno;
                    ASSERT_EQ(rc, -1);
                    ASSERT_EQ(err, ENOENT);
                }
            }
        }
    }

    END_TEST;
}

BEGIN_TEST_CASE(posixio_test)
RUN_TEST(stat_empty_test)
RUN_TEST(lstat_empty_test)
RUN_TEST(open_empty_test)
END_TEST_CASE(posixio_test)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
