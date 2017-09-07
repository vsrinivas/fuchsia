// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string_buffer.h>

#include <unittest/unittest.h>

#define EXPECT_CSTR_EQ(expected, actual) \
    EXPECT_STR_EQ(expected, actual, strlen(expected) + 1u, "unequal cstr")

namespace {

bool capacity_test() {
    BEGIN_TEST;

    {
        fbl::StringBuffer<0u> buf;
        EXPECT_EQ(0u, buf.capacity());
    }

    {
        fbl::StringBuffer<100u> buf;
        EXPECT_EQ(100u, buf.capacity());
    }

    END_TEST;
}

bool empty_string_test() {
    BEGIN_TEST;

    {
        fbl::StringBuffer<0u> empty;

        EXPECT_CSTR_EQ("", empty.data());
        EXPECT_CSTR_EQ("", empty.c_str());

        EXPECT_EQ(0u, empty.length());
        EXPECT_EQ(0u, empty.size());
        EXPECT_TRUE(empty.empty());
        EXPECT_EQ(0u, empty.capacity());

        EXPECT_CSTR_EQ("", empty.begin());
        EXPECT_EQ(0u, empty.end() - empty.begin());
        EXPECT_CSTR_EQ("", empty.cbegin());
        EXPECT_EQ(0u, empty.cend() - empty.cbegin());

        EXPECT_EQ(0, empty[0u]);
    }

    {
        fbl::StringBuffer<16u> empty;

        EXPECT_CSTR_EQ("", empty.data());
        EXPECT_CSTR_EQ("", empty.c_str());

        EXPECT_EQ(0u, empty.length());
        EXPECT_EQ(0u, empty.size());
        EXPECT_TRUE(empty.empty());
        EXPECT_EQ(16u, empty.capacity());

        EXPECT_CSTR_EQ("", empty.begin());
        EXPECT_EQ(0u, empty.end() - empty.begin());
        EXPECT_CSTR_EQ("", empty.cbegin());
        EXPECT_EQ(0u, empty.cend() - empty.cbegin());

        EXPECT_EQ(0, empty[0u]);
    }

    END_TEST;
}

bool append_test() {
    BEGIN_TEST;

    {
        fbl::StringBuffer<16u> str;
        str.Append('a')
            .Append('b')
            .Append("cd")
            .Append("efghi", 3u)
            .Append(fbl::StringPiece("hijkl", 3u))
            .Append(fbl::String("klmnopqrstuvwxyz"))
            .Append('z') // these will be truncated away
            .Append("zz")
            .Append("zzzzzz", 3u)
            .Append(fbl::StringPiece("zzzzz", 3u))
            .Append(fbl::String("zzzzz"));

        EXPECT_CSTR_EQ("abcdefghijklmnop", str.data());
        EXPECT_CSTR_EQ("abcdefghijklmnop", str.c_str());

        EXPECT_EQ(16u, str.length());
        EXPECT_EQ(16u, str.size());
        EXPECT_FALSE(str.empty());
        EXPECT_EQ(16u, str.capacity());

        EXPECT_CSTR_EQ("abcdefghijklmnop", str.begin());
        EXPECT_EQ(16u, str.end() - str.begin());
        EXPECT_CSTR_EQ("abcdefghijklmnop", str.cbegin());
        EXPECT_EQ(16u, str.cend() - str.cbegin());

        EXPECT_EQ('b', str[1u]);
    }

    {
        fbl::StringBuffer<3u> str;
        str.Append('a');
        EXPECT_CSTR_EQ("a", str.data());
        str.Append('b');
        EXPECT_CSTR_EQ("ab", str.data());
        str.Append('c');
        EXPECT_CSTR_EQ("abc", str.data());
        str.Append('d');
        EXPECT_CSTR_EQ("abc", str.data());
    }

    {
        fbl::StringBuffer<3u> str;
        str.Append("ab");
        EXPECT_CSTR_EQ("ab", str.data());
        str.Append("");
        EXPECT_CSTR_EQ("ab", str.data());
        str.Append("cdefg");
        EXPECT_CSTR_EQ("abc", str.data());
    }

    {
        fbl::StringBuffer<3u> str;
        str.Append("abcdef", 2u);
        EXPECT_CSTR_EQ("ab", str.data());
        str.Append("zzzz", 0u);
        EXPECT_CSTR_EQ("ab", str.data());
        str.Append("cdefghijk", 5u);
        EXPECT_CSTR_EQ("abc", str.data());
    }

    {
        fbl::StringBuffer<3u> str;
        str.Append(fbl::StringPiece("abcdef", 2u));
        EXPECT_CSTR_EQ("ab", str.data());
        str.Append(fbl::StringPiece("zzzz", 0u));
        EXPECT_CSTR_EQ("ab", str.data());
        str.Append(fbl::StringPiece("cdefghijk", 5u));
        EXPECT_CSTR_EQ("abc", str.data());
    }

    {
        fbl::StringBuffer<3u> str;
        str.Append(fbl::String("ab"));
        EXPECT_CSTR_EQ("ab", str.data());
        str.Append(fbl::String());
        EXPECT_CSTR_EQ("ab", str.data());
        str.Append(fbl::String("cdefg"));
        EXPECT_CSTR_EQ("abc", str.data());
    }

    END_TEST;
}

bool modify_test() {
    BEGIN_TEST;

    {
        fbl::StringBuffer<16u> str;
        str.Append("abcdef");

        EXPECT_EQ('c', str[2u]);
        str[2u] = 'x';
        EXPECT_EQ('x', str[2u]);
        EXPECT_CSTR_EQ("abxdef", str.data());

        memcpy(str.data(), "yyyy", 4u);
        EXPECT_CSTR_EQ("yyyyef", str.data());
    }

    END_TEST;
}

bool resize_test() {
    BEGIN_TEST;

    {
        fbl::StringBuffer<16u> str;

        str.Resize(4u, 'x');
        EXPECT_CSTR_EQ("xxxx", str.data());
        EXPECT_EQ(4u, str.length());

        str.Resize(8u, 'y');
        EXPECT_CSTR_EQ("xxxxyyyy", str.data());
        EXPECT_EQ(8u, str.length());

        str.Resize(16u);
        EXPECT_CSTR_EQ("xxxxyyyy", str.data());
        EXPECT_EQ(0, memcmp("xxxxyyyy\0\0\0\0\0\0\0\0\0", str.data(), str.length() + 1));
        EXPECT_EQ(16u, str.length());

        str.Resize(0u);
        EXPECT_CSTR_EQ("", str.data());
        EXPECT_EQ(0u, str.length());
    }

    END_TEST;
}

bool clear_test() {
    BEGIN_TEST;

    {
        fbl::StringBuffer<16u> str;
        str.Append("abcdef");

        str.Clear();
        EXPECT_CSTR_EQ("", str.data());
        EXPECT_EQ(0u, str.length());
    }

    END_TEST;
}

bool to_string_test() {
    BEGIN_TEST;

    {
        fbl::StringBuffer<16u> buf;
        buf.Append("abcdef");

        fbl::String str = buf.ToString();
        EXPECT_TRUE(str == "abcdef");
    }

    END_TEST;
}

bool to_string_piece_test() {
    BEGIN_TEST;

    {
        fbl::StringBuffer<16u> buf;
        buf.Append("abcdef");

        fbl::StringPiece piece = buf.ToStringPiece();
        EXPECT_EQ(buf.data(), piece.data());
        EXPECT_EQ(buf.length(), piece.length());
    }

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(string_buffer_tests)
RUN_TEST(capacity_test)
RUN_TEST(empty_string_test)
RUN_TEST(append_test)
RUN_TEST(modify_test)
RUN_TEST(resize_test)
RUN_TEST(clear_test)
RUN_TEST(to_string_test)
RUN_TEST(to_string_piece_test)
END_TEST_CASE(string_buffer_tests)
