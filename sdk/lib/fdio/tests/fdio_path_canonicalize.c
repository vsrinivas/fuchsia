// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/private.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <zxtest/zxtest.h>

#define TEST_CLEAN(p1, p2, dir)                             \
  {                                                         \
    const char in[] = (p1);                                 \
    const char out_gold[] = (p2);                           \
    size_t outlen;                                          \
    bool is_dir;                                            \
    EXPECT_OK(__fdio_cleanpath(in, out, &outlen, &is_dir)); \
    EXPECT_EQ(is_dir, dir);                                 \
    EXPECT_EQ(strcmp(out, out_gold), 0);                    \
  }

TEST(PathCanonicalizationTest, Basic) {
  char out[PATH_MAX];
  TEST_CLEAN("/foo", "/foo", false);
  TEST_CLEAN("/foo/bar/baz", "/foo/bar/baz", false);
  TEST_CLEAN("/foo/bar/baz/", "/foo/bar/baz", true);
}

TEST(PathCanonicalizationTest, DotDot) {
  char out[PATH_MAX];
  TEST_CLEAN("/foo/bar/../baz", "/foo/baz", false);
  TEST_CLEAN("/foo/bar/../baz/..", "/foo", true);
  TEST_CLEAN("/foo/bar/../baz/../", "/foo", true);
  TEST_CLEAN("../../..", "../../..", true);
  TEST_CLEAN("/../../..", "/", true);
  TEST_CLEAN("/./././../foo", "/foo", false);
}

TEST(PathCanonicalizationTest, Dot) {
  char out[PATH_MAX];
  TEST_CLEAN("/.", "/", true);
  TEST_CLEAN("/./././.", "/", true);
  TEST_CLEAN("/././././", "/", true);
  TEST_CLEAN("/foobar/././.", "/foobar", true);
  TEST_CLEAN("/foobar/./../././././///.", "/", true);
  TEST_CLEAN(".", ".", true);
  TEST_CLEAN("./.", ".", true);
  TEST_CLEAN("./././../foo", "../foo", false);
}

TEST(PathCanonicalizationTest, Minimal) {
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
}
