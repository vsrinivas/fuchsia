// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxtl/string_piece.h>

#include <unittest/unittest.h>

namespace {

bool compare_test() {
    BEGIN_TEST;

    const char data[] = "abc";
    mxtl::StringPiece a(data, 1);
    mxtl::StringPiece ab(data, 2);
    mxtl::StringPiece b(data + 1, 1);
    mxtl::StringPiece bc(data + 1, 2);

    EXPECT_TRUE(a == a, "");
    EXPECT_TRUE(ab == ab, "");
    EXPECT_TRUE(a != ab, "");
    EXPECT_TRUE(a != b, "");
    EXPECT_TRUE(ab != a, "");

    EXPECT_FALSE(a < a, "");
    EXPECT_FALSE(a > a, "");
    EXPECT_TRUE(a >= a, "");
    EXPECT_TRUE(a <= a, "");

    EXPECT_TRUE(a < ab, "");
    EXPECT_FALSE(a > ab, "");
    EXPECT_FALSE(a >= ab, "");
    EXPECT_TRUE(a <= ab, "");

    EXPECT_FALSE(ab < a, "");
    EXPECT_TRUE(ab > a, "");
    EXPECT_TRUE(ab >= a, "");
    EXPECT_FALSE(ab <= a, "");

    EXPECT_TRUE(a < b, "");
    EXPECT_FALSE(a > b, "");
    EXPECT_FALSE(a >= b, "");
    EXPECT_TRUE(a <= b, "");

    EXPECT_FALSE(b < a, "");
    EXPECT_TRUE(b > a, "");
    EXPECT_TRUE(b >= a, "");
    EXPECT_FALSE(b <= a, "");

    EXPECT_TRUE(a < bc, "");
    EXPECT_FALSE(a > bc, "");
    EXPECT_FALSE(a >= bc, "");
    EXPECT_TRUE(a <= bc, "");

    EXPECT_FALSE(bc < a, "");
    EXPECT_TRUE(bc > a, "");
    EXPECT_TRUE(bc >= a, "");
    EXPECT_FALSE(bc <= a, "");

    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(string_piece_tests)
RUN_NAMED_TEST("compare test", compare_test)
END_TEST_CASE(string_piece_tests);
