// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <zxtest/zxtest.h>

#include "sdk/lib/fdio/cleanpath.h"

#define TEST_CLEAN(p1, p2, dir)                                                               \
  {                                                                                           \
    fdio_internal::PathBuffer out;                                                            \
    const char in[] = (p1);                                                                   \
    constexpr std::string_view out_gold = (p2);                                               \
    bool is_dir;                                                                              \
    EXPECT_TRUE(fdio_internal::CleanPath(in, &out, &is_dir));                                 \
    EXPECT_EQ(is_dir, dir);                                                                   \
    EXPECT_EQ(out.length(), out_gold.length());                                               \
    EXPECT_EQ(std::string_view(out), out_gold, "\"%s\" \"%s\"", out.data(), out_gold.data()); \
  }

namespace {

TEST(PathCanonicalizationTest, Basic) {
  TEST_CLEAN("/foo", "/foo", false);
  TEST_CLEAN("/foo/bar/baz", "/foo/bar/baz", false);
  TEST_CLEAN("/foo/bar/baz/", "/foo/bar/baz", true);
}

TEST(PathCanonicalizationTest, DotDot) {
  TEST_CLEAN("/foo/bar/../baz", "/foo/baz", false);
  TEST_CLEAN("/foo/bar/../baz/..", "/foo", true);
  TEST_CLEAN("/foo/bar/../baz/../", "/foo", true);
  TEST_CLEAN("../../..", "../../..", true);
  TEST_CLEAN("/../../..", "/", true);
  TEST_CLEAN("/./././../foo", "/foo", false);
}

TEST(PathCanonicalizationTest, Dot) {
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

}  // anonymous namespace
