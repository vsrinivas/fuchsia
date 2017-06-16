// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <mxio/private.h>

#include <unittest/unittest.h>

#define TEST_CLEAN(p1, p2, dir)                                               \
    {                                                                         \
        const char in[] = (p1);                                               \
        const char out_gold[] = (p2);                                         \
        size_t outlen;                                                        \
        bool is_dir;                                                          \
        EXPECT_EQ(__mxio_cleanpath(in, out, &outlen, &is_dir), MX_OK, ""); \
        EXPECT_EQ(is_dir, dir, "");                                           \
        char msg[PATH_MAX * 3 + 128];                                         \
        sprintf(msg, "[%s] --> [%s], expected [%s]", in, out, out_gold);      \
        EXPECT_EQ(strcmp(out, out_gold), 0, msg);                             \
    }


bool basic_test(void) {
    BEGIN_TEST;
    char out[PATH_MAX];
    TEST_CLEAN("/foo", "/foo", false);
    TEST_CLEAN("/foo/bar/baz", "/foo/bar/baz", false);
    END_TEST;
}

bool dotdot_test(void) {
    BEGIN_TEST;
    char out[PATH_MAX];
    TEST_CLEAN("/foo/bar/../baz", "/foo/baz", false);
    TEST_CLEAN("/foo/bar/../baz/..", "/foo", true);
    TEST_CLEAN("/foo/bar/../baz/../", "/foo", true);
    TEST_CLEAN("../../..", "../../..", true);
    TEST_CLEAN("/../../..", "/", true);
    TEST_CLEAN("/./././../foo", "/foo", false);
    END_TEST;
}

bool dot_test(void) {
    BEGIN_TEST;
    char out[PATH_MAX];
    TEST_CLEAN("/.", "/", true);
    TEST_CLEAN("/./././.", "/", true);
    TEST_CLEAN("/././././", "/", true);
    TEST_CLEAN("/foobar/././.", "/foobar", true);
    TEST_CLEAN("/foobar/./../././././///.", "/", true);
    TEST_CLEAN(".", ".", true);
    TEST_CLEAN("./.", ".", true);
    TEST_CLEAN("./././../foo", "../foo", false);
    END_TEST;
}

bool minimal_test(void) {
    BEGIN_TEST;
    char out[PATH_MAX];
    TEST_CLEAN("", ".", true);
    TEST_CLEAN("/", "/", true);
    TEST_CLEAN("//", "/", true);
    TEST_CLEAN("///", "/", true);
    TEST_CLEAN("a", "a", false);
    TEST_CLEAN("a/", "a", true);
    TEST_CLEAN("a/.", "a", true);
    TEST_CLEAN("a/..", ".", true);
    TEST_CLEAN("a/../.", ".", true);
    TEST_CLEAN("/a/../.", "/", true);
    TEST_CLEAN(".", ".", true);
    TEST_CLEAN("..", "..", true);
    TEST_CLEAN("...", "...", false);
    END_TEST;
}

BEGIN_TEST_CASE(mxio_path_canonicalization_test)
RUN_TEST(basic_test);
RUN_TEST(dotdot_test);
RUN_TEST(dot_test);
RUN_TEST(minimal_test);
END_TEST_CASE(mxio_path_canonicalization_test)
