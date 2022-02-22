// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string_view>

#include <fbl/string_buffer.h>
#include <zxtest/zxtest.h>

#define EXPECT_DATA_AND_LENGTH(expected, actual)  \
  do {                                            \
    EXPECT_STREQ(expected, actual.data());        \
    EXPECT_EQ(strlen(expected), actual.length()); \
  } while (false)

namespace {

// Note: |runnable| can't be a reference since that'd make the behavior of
// |va_start()| undefined.
template <typename Runnable>
void VAListHelper(Runnable runnable, ...) {
  va_list ap;
  va_start(ap, runnable);
  runnable(ap);
  va_end(ap);
}

TEST(StringBufferTest, Constructors) {
  {
    fbl::StringBuffer<0u> buf;
    EXPECT_EQ(0, buf.length());
    EXPECT_EQ('\0', buf[0]);
  }

  {
    fbl::StringBuffer<1u> buf('.');
    EXPECT_EQ(1, buf.length());
    EXPECT_EQ('.', buf[0]);
    EXPECT_EQ('\0', buf[1]);
  }

  {
    fbl::StringBuffer<2u> buf('.');
    EXPECT_EQ(1, buf.length());
    EXPECT_EQ('.', buf[0]);
    EXPECT_EQ('\0', buf[1]);
  }
}

TEST(StringBufferTest, Capacity) {
  {
    fbl::StringBuffer<0u> buf;
    EXPECT_EQ(0u, buf.capacity());
  }

  {
    fbl::StringBuffer<100u> buf;
    EXPECT_EQ(100u, buf.capacity());
  }
}

TEST(StringBufferTest, EmptyString) {
  {
    fbl::StringBuffer<0u> empty;

    EXPECT_STREQ("", empty.data());
    EXPECT_STREQ("", empty.c_str());

    EXPECT_EQ(0u, empty.length());
    EXPECT_EQ(0u, empty.size());
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(0u, empty.capacity());

    EXPECT_STREQ("", empty.begin());
    EXPECT_EQ(0u, empty.end() - empty.begin());
    EXPECT_STREQ("", empty.cbegin());
    EXPECT_EQ(0u, empty.cend() - empty.cbegin());

    EXPECT_EQ(0, empty[0u]);
  }

  {
    fbl::StringBuffer<16u> empty;

    EXPECT_STREQ("", empty.data());
    EXPECT_STREQ("", empty.c_str());

    EXPECT_EQ(0u, empty.length());
    EXPECT_EQ(0u, empty.size());
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(16u, empty.capacity());

    EXPECT_STREQ("", empty.begin());
    EXPECT_EQ(0u, empty.end() - empty.begin());
    EXPECT_STREQ("", empty.cbegin());
    EXPECT_EQ(0u, empty.cend() - empty.cbegin());

    EXPECT_EQ(0, empty[0u]);
  }
}

TEST(StringBufferTest, Append) {
  {
    fbl::StringBuffer<16u> str;
    str.Append('a')
        .Append('b')
        .Append("cd")
        .Append("efghi", 3u)
        .Append(std::string_view("hijkl", 3u))
        .Append(fbl::String("klmnopqrstuvwxyz"))
        .Append('z')  // these will be truncated away
        .Append("zz")
        .Append("zzzzzz", 3u)
        .Append(std::string_view("zzzzz", 3u))
        .Append(fbl::String("zzzzz"));

    EXPECT_STREQ("abcdefghijklmnop", str.data());
    EXPECT_STREQ("abcdefghijklmnop", str.c_str());

    EXPECT_EQ(16u, str.length());
    EXPECT_EQ(16u, str.size());
    EXPECT_FALSE(str.empty());
    EXPECT_EQ(16u, str.capacity());

    EXPECT_STREQ("abcdefghijklmnop", str.begin());
    EXPECT_EQ(16u, str.end() - str.begin());
    EXPECT_STREQ("abcdefghijklmnop", str.cbegin());
    EXPECT_EQ(16u, str.cend() - str.cbegin());

    EXPECT_EQ('b', str[1u]);
  }

  {
    fbl::StringBuffer<3u> str;
    str.Append('a');
    EXPECT_DATA_AND_LENGTH("a", str);
    str.Append('b');
    EXPECT_DATA_AND_LENGTH("ab", str);
    str.Append('c');
    EXPECT_DATA_AND_LENGTH("abc", str);
    str.Append('d');
    EXPECT_DATA_AND_LENGTH("abc", str);
  }

  {
    fbl::StringBuffer<3u> str;
    str.Append("ab");
    EXPECT_DATA_AND_LENGTH("ab", str);
    str.Append("");
    EXPECT_DATA_AND_LENGTH("ab", str);
    str.Append("cdefg");
    EXPECT_DATA_AND_LENGTH("abc", str);
  }

  {
    fbl::StringBuffer<3u> str;
    str.Append("abcdef", 2u);
    EXPECT_DATA_AND_LENGTH("ab", str);
    str.Append("zzzz", 0u);
    EXPECT_DATA_AND_LENGTH("ab", str);
    str.Append("cdefghijk", 5u);
    EXPECT_DATA_AND_LENGTH("abc", str);
  }

  {
    fbl::StringBuffer<3u> str;
    str.Append(std::string_view("abcdef", 2u));
    EXPECT_DATA_AND_LENGTH("ab", str);
    str.Append(std::string_view("zzzz", 0u));
    EXPECT_DATA_AND_LENGTH("ab", str);
    str.Append(std::string_view("cdefghijk", 5u));
    EXPECT_DATA_AND_LENGTH("abc", str);
  }

  {
    fbl::StringBuffer<3u> str;
    str.Append(fbl::String("ab"));
    EXPECT_DATA_AND_LENGTH("ab", str);
    str.Append(fbl::String());
    EXPECT_DATA_AND_LENGTH("ab", str);
    str.Append(fbl::String("cdefg"));
    EXPECT_DATA_AND_LENGTH("abc", str);
  }
}

TEST(StringBufferTest, AppendPrintf) {
  {
    fbl::StringBuffer<12u> str;
    str.AppendPrintf("abc");
    EXPECT_DATA_AND_LENGTH("abc", str);
    str.AppendPrintf("%d,%s", 20, "de").Append('f');
    EXPECT_DATA_AND_LENGTH("abc20,def", str);
    str.AppendPrintf("%d", 123456789);
    EXPECT_DATA_AND_LENGTH("abc20,def123", str);
  }

  {
    fbl::StringBuffer<12u> str;
    VAListHelper([&str](va_list ap) { str.AppendVPrintf("abc", ap); });
    EXPECT_DATA_AND_LENGTH("abc", str);
    VAListHelper([&str](va_list ap) { str.AppendVPrintf("%d,%s", ap).Append('f'); }, 20, "de");
    EXPECT_DATA_AND_LENGTH("abc20,def", str);
    VAListHelper([&str](va_list ap) { str.AppendVPrintf("%d", ap); }, 123456789);
    EXPECT_DATA_AND_LENGTH("abc20,def123", str);
  }
}

TEST(StringBufferTest, Modify) {
  fbl::StringBuffer<16u> str;
  str.Append("abcdef");

  EXPECT_EQ('c', str[2u]);
  str[2u] = 'x';
  EXPECT_EQ('x', str[2u]);
  EXPECT_DATA_AND_LENGTH("abxdef", str);

  memcpy(str.data(), "yyyy", 4u);
  EXPECT_DATA_AND_LENGTH("yyyyef", str);
}

TEST(StringBufferTest, Set) {
  fbl::StringBuffer<16u> str;

  str.Append("foo");
  EXPECT_STREQ("foo", str.data());
  EXPECT_EQ(3, str.length());

  str.Set("longer");
  EXPECT_STREQ("longer", str.data());
  EXPECT_EQ(6, str.length());

  str.Set("short");
  EXPECT_STREQ("short", str.data());
  EXPECT_EQ(5, str.length());
}

TEST(StringBufferTest, Resize) {
  fbl::StringBuffer<16u> str;

  str.Resize(4u, 'x');
  EXPECT_STREQ("xxxx", str.data());
  EXPECT_EQ(4u, str.length());

  str.Resize(8u, 'y');
  EXPECT_STREQ("xxxxyyyy", str.data());
  EXPECT_EQ(8u, str.length());

  str.Resize(16u);
  EXPECT_STREQ("xxxxyyyy", str.data());
  EXPECT_EQ(0, memcmp("xxxxyyyy\0\0\0\0\0\0\0\0\0", str.data(), str.length() + 1));
  EXPECT_EQ(16u, str.length());

  str.Resize(0u);
  EXPECT_STREQ("", str.data());
  EXPECT_EQ(0u, str.length());
}

TEST(StringBufferTest, Clear) {
  fbl::StringBuffer<16u> str;
  str.Append("abcdef");

  str.Clear();
  EXPECT_STREQ("", str.data());
  EXPECT_EQ(0u, str.length());
}

TEST(StringBufferTest, RemovePrefix) {
  fbl::StringBuffer<16u> str;
  str.Append("abcdef");

  str.RemovePrefix(4);
  EXPECT_STREQ("ef", str.data());
  EXPECT_EQ(2u, str.length());

  str.RemovePrefix(2);
  EXPECT_STREQ("", str.data());
  EXPECT_EQ(0u, str.length());
}

TEST(StringBufferTest, ToString) {
  fbl::StringBuffer<16u> buf;
  buf.Append("abcdef");

  fbl::String str = buf.ToString();
  EXPECT_TRUE(str == "abcdef");
}

TEST(StringBufferTest, ToStringPiece) {
  fbl::StringBuffer<16u> buf;
  buf.Append("abcdef");

  std::string_view piece = buf;
  EXPECT_EQ(buf.data(), piece.data());
  EXPECT_EQ(buf.length(), piece.length());
}

}  // namespace
