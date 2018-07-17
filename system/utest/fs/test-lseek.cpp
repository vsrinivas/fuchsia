// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zircon/compiler.h>
#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>

#include "filesystems.h"

namespace {

bool test_lseek_position(void) {
    BEGIN_TEST;

    const char* const filename = "::lseek_position";
    fbl::unique_fd fd(open(filename, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd);

    // File offset initialized to zero.
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_CUR), 0);
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);

    const char* const str = "hello";
    const size_t len = strlen(str);
    ASSERT_EQ(write(fd.get(), str, len), len);

    // After writing, the offset has been updated.
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_CUR), len);
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_END), len);

    // Reset the offset to the start of the file.
    ASSERT_EQ(lseek(fd.get(), -len, SEEK_END), 0);

    // Read the entire file.
    char buf[len + 1];
    ASSERT_EQ(read(fd.get(), buf, len), len);
    ASSERT_EQ(memcmp(buf, str, len), 0);

    // Seek and read part of the file.
    ASSERT_EQ(lseek(fd.get(), 1, SEEK_SET), 1);
    ASSERT_EQ(read(fd.get(), buf, len - 1), len - 1);
    ASSERT_EQ(memcmp(buf, &str[1], len - 1), 0);

    ASSERT_EQ(unlink(filename), 0);
    END_TEST;
}

bool test_lseek_out_of_bounds(void) {
    BEGIN_TEST;

    const char* const filename = "::lseek_out_of_bounds";
    fbl::unique_fd fd(open(filename, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd);

    const char* const str = "hello";
    const size_t len = strlen(str);
    ASSERT_EQ(write(fd.get(), str, len), len);

    // After writing, the offset has been updated.
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_CUR), len);

    // Seek beyond the end of the file.
    ASSERT_EQ(lseek(fd.get(), 1, SEEK_CUR), len + 1);
    ASSERT_EQ(lseek(fd.get(), 2, SEEK_END), len + 2);
    ASSERT_EQ(lseek(fd.get(), len + 3, SEEK_SET), len + 3);

    // Seek before the start of the file.
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);

    // Negative seek is not allowed on Fuchsia.
    ASSERT_EQ(lseek(fd.get(), -2, SEEK_CUR), -1);
    ASSERT_EQ(lseek(fd.get(), -2, SEEK_SET), -1);
    ASSERT_EQ(lseek(fd.get(), -(len + 2), SEEK_END), -1);

    ASSERT_EQ(unlink(filename), 0);
    END_TEST;
}

bool test_lseek_zero_fill(void) {
    BEGIN_TEST;

    const char* const filename = "::lseek_zero_fill";
    fbl::unique_fd fd(open(filename, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd);

    const char* const str = "hello";
    const size_t len = strlen(str);
    ASSERT_EQ(write(fd.get(), str, len), len);

    // After writing, the offset and length have been updated.
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_CUR), len);
    struct stat st;
    ASSERT_EQ(fstat(fd.get(), &st), 0);
    ASSERT_EQ(st.st_size, len);

    // Seek beyond the end of the file.
    size_t zeros = 10;
    ASSERT_EQ(lseek(fd.get(), len + zeros, SEEK_SET), static_cast<off_t>(len + zeros));

    // This does not change the length of the file.
    ASSERT_EQ(fstat(fd.get(), &st), 0);
    ASSERT_EQ(st.st_size, len);

    // From the POSIX specification:
    //
    // "Before any action described below is taken, and if nbyte is zero and the
    // file is a regular file, the write() function may detect and return
    // errors as described below. In the absence of errors, or if error
    // detection is not performed, the write() function shall return zero
    // and have no other results."
    ASSERT_EQ(write(fd.get(), str, 0), 0);
    ASSERT_EQ(fstat(fd.get(), &st), 0);
    ASSERT_EQ(st.st_size, len);

    // Zero-extend the file up to the sentinel value.
    char sentinel = 'a';
    ASSERT_EQ(write(fd.get(), &sentinel, 1), 1);
    ASSERT_EQ(fstat(fd.get(), &st), 0);
    ASSERT_EQ(st.st_size, static_cast<off_t>(len + zeros + 1));

    // Validate the file contents.
    {
        char expected[len + zeros + 1];
        memcpy(expected, str, len);
        memset(&expected[len], 0, zeros);
        expected[len + zeros] = 'a';

        char buf[len + zeros + 1];
        ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
        ASSERT_EQ(read(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));
        ASSERT_EQ(memcmp(buf, expected, sizeof(expected)), 0);
    }

    // Truncate and observe the (old) sentinel value has been
    // overwritten with zeros.
    ASSERT_EQ(ftruncate(fd.get(), len), 0);
    zeros *= 2;
    ASSERT_EQ(lseek(fd.get(), len + zeros, SEEK_SET), static_cast<off_t>(len + zeros));
    ASSERT_EQ(write(fd.get(), &sentinel, 1), 1);
    ASSERT_EQ(fstat(fd.get(), &st), 0);
    ASSERT_EQ(st.st_size, static_cast<off_t>(len + zeros + 1));

    {
        char expected[len + zeros + 1];
        memcpy(expected, str, len);
        memset(&expected[len], 0, zeros);
        expected[len + zeros] = 'a';

        char buf[len + zeros + 1];
        ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
        ASSERT_EQ(read(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));
        ASSERT_EQ(memcmp(buf, expected, sizeof(expected)), 0);
    }

    ASSERT_EQ(unlink(filename), 0);
    END_TEST;
}

}  // namespace

RUN_FOR_ALL_FILESYSTEMS(lseek_tests,
    RUN_TEST_MEDIUM(test_lseek_position)
    RUN_TEST_MEDIUM(test_lseek_out_of_bounds)
    RUN_TEST_MEDIUM(test_lseek_zero_fill)
)
