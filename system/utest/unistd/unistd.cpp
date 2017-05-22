// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <unittest/unittest.h>

bool fstat_test(void) {
    BEGIN_TEST;

    struct stat buf;
    EXPECT_EQ(0, fstat(0, &buf), "fstat 0");

    int pipe_fds[2];
    EXPECT_EQ(0, pipe(pipe_fds), "pipe");

    int res = fstat(pipe_fds[0], &buf);
    if (res != 0) {
        unittest_printf_critical("\nfstat pipe errno %d \"%s\"\n", errno, strerror(errno));
        EXPECT_TRUE(false, "fstat pipe");
    }

    EXPECT_EQ(0, close(pipe_fds[0]), "close pipe_fds[0]");
    EXPECT_EQ(0, close(pipe_fds[1]), "close pipe_fds[1]");

    int tmp_fd = open("/tmp/unistd-test-file", O_CREAT);
    EXPECT_NEQ(-1, tmp_fd, "create temp file");

    res = fstat(tmp_fd, &buf);
    if (res != 0) {
        unittest_printf_critical("\nfstat tempfile errno %d \"%s\"\n", errno, strerror(errno));
        EXPECT_TRUE(false, "fstat temp file");
    }

    EXPECT_EQ(0, close(tmp_fd), "close tmp_fd");

    int null_fd = open("/dev/null", O_RDONLY);
    EXPECT_NEQ(-1, null_fd, "open /dev/null");

    res = fstat(null_fd, &buf);
    if (res != 0) {
        unittest_printf_critical("\nfstat /dev/null errno %d \"%s\"\n", errno, strerror(errno));
        EXPECT_TRUE(false, "fstat /dev/null");
    }
    EXPECT_EQ(0, close(null_fd), "close /dev/null");

    END_TEST;
}


BEGIN_TEST_CASE(unistd_tests)
RUN_TEST(fstat_test)
END_TEST_CASE(unistd_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
