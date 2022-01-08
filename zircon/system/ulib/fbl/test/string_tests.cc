// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>
#include <string_view>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <zxtest/zxtest.h>

namespace fbl {
namespace tests {
struct StringTestHelper {
  static unsigned int GetRefCount(const String& str) { return str.ref_count(); }
};
}  // namespace tests
}  // namespace fbl

using fbl::tests::StringTestHelper;

namespace {

TEST(StringTest, Empty) {
  {
    fbl::String empty;

    EXPECT_STREQ("", empty.data());
    EXPECT_STREQ("", empty.c_str());

    EXPECT_EQ(0u, empty.length());
    EXPECT_EQ(0u, empty.size());
    EXPECT_TRUE(empty.empty());

    EXPECT_STREQ("", empty.begin());
    EXPECT_EQ(0u, empty.end() - empty.begin());
    EXPECT_STREQ("", empty.cbegin());
    EXPECT_EQ(0u, empty.cend() - empty.cbegin());

    EXPECT_EQ(0, empty[0u]);
  }

  {
    fbl::String empty("");

    EXPECT_STREQ("", empty.data());
    EXPECT_STREQ("", empty.c_str());

    EXPECT_EQ(0u, empty.length());
    EXPECT_EQ(0u, empty.size());
    EXPECT_TRUE(empty.empty());

    EXPECT_STREQ("", empty.begin());
    EXPECT_EQ(0u, empty.end() - empty.begin());
    EXPECT_STREQ("", empty.cbegin());
    EXPECT_EQ(0u, empty.cend() - empty.cbegin());

    EXPECT_EQ(0, empty[0u]);
  }

  {
    fbl::String empty("abcde", size_t(0u));

    EXPECT_STREQ("", empty.data());
    EXPECT_STREQ("", empty.c_str());

    EXPECT_EQ(0u, empty.length());
    EXPECT_EQ(0u, empty.size());
    EXPECT_TRUE(empty.empty());

    EXPECT_STREQ("", empty.begin());
    EXPECT_EQ(0u, empty.end() - empty.begin());
    EXPECT_STREQ("", empty.cbegin());
    EXPECT_EQ(0u, empty.cend() - empty.cbegin());

    EXPECT_EQ(0, empty[0u]);
  }

  {
    fbl::String empty(0u, 'x');

    EXPECT_STREQ("", empty.data());
    EXPECT_STREQ("", empty.c_str());

    EXPECT_EQ(0u, empty.length());
    EXPECT_EQ(0u, empty.size());
    EXPECT_TRUE(empty.empty());

    EXPECT_STREQ("", empty.begin());
    EXPECT_EQ(0u, empty.end() - empty.begin());
    EXPECT_STREQ("", empty.cbegin());
    EXPECT_EQ(0u, empty.cend() - empty.cbegin());

    EXPECT_EQ(0, empty[0u]);
  }

  {
    fbl::String empty(std::string_view("abcde", 0u));

    EXPECT_STREQ("", empty.data());
    EXPECT_STREQ("", empty.c_str());

    EXPECT_EQ(0u, empty.length());
    EXPECT_EQ(0u, empty.size());
    EXPECT_TRUE(empty.empty());

    EXPECT_STREQ("", empty.begin());
    EXPECT_EQ(0u, empty.end() - empty.begin());
    EXPECT_STREQ("", empty.cbegin());
    EXPECT_EQ(0u, empty.cend() - empty.cbegin());

    EXPECT_EQ(0, empty[0u]);
  }
}

TEST(StringTest, NonEmpty) {
  {
    fbl::String str("abc");

    EXPECT_STREQ("abc", str.data());

    EXPECT_EQ(3u, str.length());
    EXPECT_EQ(3u, str.size());
    EXPECT_FALSE(str.empty());

    EXPECT_STREQ("abc", str.begin());
    EXPECT_EQ(3u, str.end() - str.begin());
    EXPECT_STREQ("abc", str.cbegin());
    EXPECT_EQ(3u, str.cend() - str.cbegin());

    EXPECT_EQ('b', str[1u]);
  }

  {
    fbl::String str("abc", 2u);

    EXPECT_STREQ("ab", str.data());

    EXPECT_EQ(2u, str.length());
    EXPECT_EQ(2u, str.size());
    EXPECT_FALSE(str.empty());

    EXPECT_STREQ("ab", str.begin());
    EXPECT_EQ(2u, str.end() - str.begin());
    EXPECT_STREQ("ab", str.cbegin());
    EXPECT_EQ(2u, str.cend() - str.cbegin());

    EXPECT_EQ('b', str[1u]);
  }

  {
    fbl::String str(10u, 'x');

    EXPECT_STREQ("xxxxxxxxxx", str.data());

    EXPECT_EQ(10u, str.length());
    EXPECT_EQ(10u, str.size());
    EXPECT_FALSE(str.empty());

    EXPECT_STREQ("xxxxxxxxxx", str.begin());
    EXPECT_EQ(10u, str.end() - str.begin());
    EXPECT_STREQ("xxxxxxxxxx", str.cbegin());
    EXPECT_EQ(10u, str.cend() - str.cbegin());

    EXPECT_EQ('x', str[1u]);
  }

  {
    fbl::String str(std::string_view("abcdef", 2u));

    EXPECT_STREQ("ab", str.data());

    EXPECT_EQ(2u, str.length());
    EXPECT_EQ(2u, str.size());
    EXPECT_FALSE(str.empty());

    EXPECT_STREQ("ab", str.begin());
    EXPECT_EQ(2u, str.end() - str.begin());
    EXPECT_STREQ("ab", str.cbegin());
    EXPECT_EQ(2u, str.cend() - str.cbegin());

    EXPECT_EQ('b', str[1u]);
  }
}

TEST(StringTest, CopyMoveAndAssignment) {
  {
    fbl::String abc("abc");
    fbl::String copy(abc);
    EXPECT_STREQ("abc", abc.data());
    EXPECT_EQ(abc.data(), copy.data());
    EXPECT_EQ(3u, copy.length());
  }

  {
    fbl::String abc("abc");
    fbl::String copy(abc);
    fbl::String move(std::move(copy));
    EXPECT_STREQ("abc", abc.data());
    EXPECT_STREQ("", copy.data());
    EXPECT_EQ(abc.data(), move.data());
    EXPECT_EQ(3u, move.length());
  }

  {
    fbl::String abc("abc");
    fbl::String str;
    str = abc;
    EXPECT_STREQ("abc", abc.data());
    EXPECT_EQ(abc.data(), str.data());
    EXPECT_EQ(3u, str.length());
  }

  {
    fbl::String abc("abc");
    fbl::String copy(abc);
    fbl::String str;
    str = std::move(copy);
    EXPECT_STREQ("abc", abc.data());
    EXPECT_STREQ("", copy.data());
    EXPECT_EQ(abc.data(), str.data());
    EXPECT_EQ(3u, str.length());
  }

  {
    fbl::String str;
    str = "abc";
    EXPECT_STREQ("abc", str.data());
    EXPECT_EQ(3u, str.length());

    str = "";
    EXPECT_STREQ("", str.data());
    EXPECT_EQ(0u, str.length());

    fbl::String copy(str);
    EXPECT_STREQ("", copy.data());
    EXPECT_EQ(0u, copy.length());

    fbl::String move(copy);
    EXPECT_STREQ("", copy.data());
    EXPECT_EQ(0u, copy.length());
    EXPECT_STREQ("", move.data());
    EXPECT_EQ(0u, move.length());
  }
}

TEST(StringTest, Clear) {
  fbl::String str = "abc";
  EXPECT_STREQ("abc", str.data());
  EXPECT_EQ(3u, str.length());

  str.clear();
  EXPECT_STREQ("", str.data());
  EXPECT_EQ(0u, str.length());
}

TEST(StringTest, Compare) {
  const char data[] = "abc";
  fbl::String empty;
  fbl::String a(data, 1);
  fbl::String ab(data, 2);
  fbl::String b(data + 1, 1);
  fbl::String bc(data + 1, 2);

  EXPECT_EQ(0, empty.compare(empty));
  EXPECT_LT(empty.compare(a), 0);
  EXPECT_GT(a.compare(empty), 0);

  EXPECT_EQ(0, a.compare(a));
  EXPECT_EQ(0, ab.compare(ab));
  EXPECT_LT(a.compare(ab), 0);
  EXPECT_GT(ab.compare(a), 0);
  EXPECT_LT(ab.compare(bc), 0);
  EXPECT_GT(bc.compare(ab), 0);

  EXPECT_TRUE(empty == empty);
  EXPECT_TRUE(empty <= empty);
  EXPECT_TRUE(empty >= empty);
  EXPECT_FALSE(empty != empty);
  EXPECT_FALSE(empty < empty);
  EXPECT_FALSE(empty > empty);
  EXPECT_TRUE(empty < a);
  EXPECT_TRUE(a > empty);

  EXPECT_TRUE(a == a);
  EXPECT_TRUE(ab == ab);
  EXPECT_TRUE(a != ab);
  EXPECT_TRUE(a != b);
  EXPECT_TRUE(ab != a);

  EXPECT_FALSE(a < a);
  EXPECT_FALSE(a > a);
  EXPECT_TRUE(a >= a);
  EXPECT_TRUE(a <= a);

  EXPECT_TRUE(a < ab);
  EXPECT_FALSE(a > ab);
  EXPECT_FALSE(a >= ab);
  EXPECT_TRUE(a <= ab);

  EXPECT_FALSE(ab < a);
  EXPECT_TRUE(ab > a);
  EXPECT_TRUE(ab >= a);
  EXPECT_FALSE(ab <= a);

  EXPECT_TRUE(a < b);
  EXPECT_FALSE(a > b);
  EXPECT_FALSE(a >= b);
  EXPECT_TRUE(a <= b);

  EXPECT_FALSE(b < a);
  EXPECT_TRUE(b > a);
  EXPECT_TRUE(b >= a);
  EXPECT_FALSE(b <= a);

  EXPECT_TRUE(a < bc);
  EXPECT_FALSE(a > bc);
  EXPECT_FALSE(a >= bc);
  EXPECT_TRUE(a <= bc);

  EXPECT_FALSE(bc < a);
  EXPECT_TRUE(bc > a);
  EXPECT_TRUE(bc >= a);
  EXPECT_FALSE(bc <= a);
}

TEST(StringTest, Concat) {
  {
    fbl::String empty = fbl::String::Concat({});
    EXPECT_STREQ("", empty.c_str());
    EXPECT_EQ(0u, empty.length());
  }

  {
    fbl::String empty = fbl::String::Concat({""});
    EXPECT_STREQ("", empty.c_str());
    EXPECT_EQ(0u, empty.length());
  }

  {
    fbl::String empty = fbl::String::Concat({"", "", "", ""});
    EXPECT_STREQ("", empty.c_str());
    EXPECT_EQ(0u, empty.length());
  }

  {
    fbl::String str = fbl::String::Concat({"abc"});
    EXPECT_STREQ("abc", str.c_str());
    EXPECT_EQ(3u, str.length());
  }

  {
    fbl::String str = fbl::String::Concat({"abc", "def"});
    EXPECT_STREQ("abcdef", str.c_str());
    EXPECT_EQ(6u, str.length());
  }

  {
    fbl::String str = fbl::String::Concat({"abc", "", "def"});
    EXPECT_STREQ("abcdef", str.c_str());
    EXPECT_EQ(6u, str.length());
  }

  {
    fbl::String str = fbl::String::Concat({"abc", "def", ""});
    EXPECT_STREQ("abcdef", str.c_str());
    EXPECT_EQ(6u, str.length());
  }

  {
    fbl::String str = fbl::String::Concat({"", "abc", "def"});
    EXPECT_STREQ("abcdef", str.c_str());
    EXPECT_EQ(6u, str.length());
  }

  {
    fbl::String str = fbl::String::Concat({"abc", "def", "g", "hi", "jklmnop"});
    EXPECT_STREQ("abcdefghijklmnop", str.c_str());
    EXPECT_EQ(16u, str.length());
  }
}

TEST(StringTest, ToString) {
  {
    fbl::String empty;
    std::string_view piece(empty);
    EXPECT_EQ(empty.data(), piece.data());
    EXPECT_EQ(0u, piece.length());
  }

  {
    fbl::String str("abc");
    std::string_view piece(str);
    EXPECT_EQ(str.data(), piece.data());
    EXPECT_EQ(3u, piece.length());
  }
}

TEST(StringTest, ToStringPiece) {
  {
    fbl::String empty;
    std::string_view view(empty);
    EXPECT_EQ(empty.data(), view.data());
    EXPECT_EQ(0u, view.length());
  }

  {
    fbl::String str("abc");
    std::string_view view(str);
    EXPECT_EQ(str.data(), view.data());
    EXPECT_EQ(3u, view.length());
  }
}

TEST(StringTest, Swap) {
  fbl::String empty;
  fbl::String abc("abc");
  fbl::String def("def");

  abc.swap(def);
  empty.swap(abc);

  EXPECT_STREQ("def", empty.data());
  EXPECT_STREQ("", abc.data());
  EXPECT_STREQ("abc", def.data());
}

TEST(StringTest, RefCount) {
  // Empty strings

  {
    fbl::String empty;
    unsigned int initial_ref_count = StringTestHelper::GetRefCount(empty);
    EXPECT_GT(initial_ref_count, 1u);
    {
      fbl::String copy(empty);
      EXPECT_EQ(empty.data(), copy.data());
      EXPECT_EQ(initial_ref_count + 1u, StringTestHelper::GetRefCount(empty));
      {
        fbl::String another_empty("");
        EXPECT_EQ(empty.data(), another_empty.data());
        EXPECT_EQ(initial_ref_count + 2u, StringTestHelper::GetRefCount(empty));
        {
          fbl::String assigned_from_empty = another_empty;
          EXPECT_EQ(empty.data(), assigned_from_empty.data());
          EXPECT_EQ(initial_ref_count + 3u, StringTestHelper::GetRefCount(empty));

          assigned_from_empty = "";
          EXPECT_EQ(empty.data(), assigned_from_empty.data());
          EXPECT_EQ(initial_ref_count + 3u, StringTestHelper::GetRefCount(empty));

          assigned_from_empty = empty;
          EXPECT_EQ(empty.data(), assigned_from_empty.data());
          EXPECT_EQ(initial_ref_count + 3u, StringTestHelper::GetRefCount(empty));

          assigned_from_empty.clear();
          EXPECT_EQ(empty.data(), assigned_from_empty.data());
          EXPECT_EQ(initial_ref_count + 3u, StringTestHelper::GetRefCount(empty));
        }
        EXPECT_EQ(initial_ref_count + 2u, StringTestHelper::GetRefCount(empty));
      }
      EXPECT_EQ(initial_ref_count + 1u, StringTestHelper::GetRefCount(empty));
    }
    EXPECT_EQ(initial_ref_count, StringTestHelper::GetRefCount(empty));
  }

  // C-string initialized strings.

  {
    fbl::String abc("abc");
    EXPECT_EQ(1u, StringTestHelper::GetRefCount(abc));
    {
      fbl::String copy1(abc);
      EXPECT_EQ(abc.data(), copy1.data());
      EXPECT_EQ(2u, StringTestHelper::GetRefCount(abc));
      {
        fbl::String copy2(abc);
        EXPECT_EQ(abc.data(), copy2.data());
        EXPECT_EQ(3u, StringTestHelper::GetRefCount(abc));
        {
          fbl::String assigned_from_abc = abc;
          EXPECT_EQ(abc.data(), assigned_from_abc.data());
          EXPECT_EQ(4u, StringTestHelper::GetRefCount(abc));

          assigned_from_abc = "";
          EXPECT_STREQ("", assigned_from_abc.data());
          EXPECT_EQ(3u, StringTestHelper::GetRefCount(abc));

          assigned_from_abc = abc;
          EXPECT_EQ(abc.data(), assigned_from_abc.data());
          EXPECT_EQ(4u, StringTestHelper::GetRefCount(abc));

          assigned_from_abc.clear();
          EXPECT_STREQ("", assigned_from_abc.data());
          EXPECT_EQ(3u, StringTestHelper::GetRefCount(abc));
        }
        EXPECT_EQ(3u, StringTestHelper::GetRefCount(abc));
      }
      EXPECT_EQ(2u, StringTestHelper::GetRefCount(abc));
    }
    EXPECT_EQ(1u, StringTestHelper::GetRefCount(abc));
  }

  // Repeated character initialized strings.

  {
    fbl::String xs(10u, 'x');
    EXPECT_EQ(1u, StringTestHelper::GetRefCount(xs));
    {
      fbl::String copy1(xs);
      EXPECT_EQ(xs.data(), copy1.data());
      EXPECT_EQ(2u, StringTestHelper::GetRefCount(xs));
      {
        fbl::String copy2(xs);
        EXPECT_EQ(xs.data(), copy2.data());
        EXPECT_EQ(3u, StringTestHelper::GetRefCount(xs));
        {
          fbl::String assigned_from_xs = xs;
          EXPECT_EQ(xs.data(), assigned_from_xs.data());
          EXPECT_EQ(4u, StringTestHelper::GetRefCount(xs));

          assigned_from_xs = "";
          EXPECT_STREQ("", assigned_from_xs.data());
          EXPECT_EQ(3u, StringTestHelper::GetRefCount(xs));

          assigned_from_xs = xs;
          EXPECT_EQ(xs.data(), assigned_from_xs.data());
          EXPECT_EQ(4u, StringTestHelper::GetRefCount(xs));

          assigned_from_xs.clear();
          EXPECT_STREQ("", assigned_from_xs.data());
          EXPECT_EQ(3u, StringTestHelper::GetRefCount(xs));
        }
        EXPECT_EQ(3u, StringTestHelper::GetRefCount(xs));
      }
      EXPECT_EQ(2u, StringTestHelper::GetRefCount(xs));
    }
    EXPECT_EQ(1u, StringTestHelper::GetRefCount(xs));
  }
}

}  // namespace
