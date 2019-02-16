// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/random.h>

#include <errno.h>
#include <unittest/unittest.h>

bool getentropy_valid() {
    BEGIN_TEST;

    char buf[16];

    errno = 0;
    int result = getentropy(buf, sizeof(buf));
    int err = errno;

    EXPECT_EQ(result, 0);
    EXPECT_EQ(err, 0);

    END_TEST;
}

bool getentropy_too_big() {
    BEGIN_TEST;

    const size_t size = 1024 * 1024 * 1024;

    char* buf = static_cast<char*>(malloc(size));
    EXPECT_NONNULL(buf);

    errno = 0;
    int result = getentropy(buf, size);
    int err = errno;

    EXPECT_EQ(result, -1);
    EXPECT_EQ(err, EIO);

    free(buf);

    END_TEST;
}

BEGIN_TEST_CASE(getentropy_tests)
RUN_TEST(getentropy_valid);
RUN_TEST(getentropy_too_big);
END_TEST_CASE(getentropy_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
