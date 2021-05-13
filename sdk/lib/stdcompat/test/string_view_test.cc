// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/string_view.h>

#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <gtest/gtest.h>

#include "test_helper.h"

namespace {

TEST(StringViewTest, CreateFromStdString) {
  std::string str = "1";
  cpp17::string_view v_str(str);

  EXPECT_FALSE(v_str.empty());
  EXPECT_EQ(str.data(), v_str.data());
  EXPECT_EQ(str.length(), v_str.length());
}

TEST(StringViewTest, CreateFromCArray) {
  constexpr char kStr[] = "1";
  cpp17::string_view v_str(kStr);

  EXPECT_FALSE(v_str.empty());
  EXPECT_EQ(kStr, v_str.data());
  EXPECT_EQ(std::strlen(kStr), v_str.length());
}

TEST(StringViewTest, CreateFromConstChar) {
  const char* kStr = "1";
  cpp17::string_view v_str(kStr);

  EXPECT_FALSE(v_str.empty());
  EXPECT_EQ(kStr, v_str.data());
  EXPECT_EQ(std::strlen(kStr), v_str.length());
}

TEST(StringViewTest, CreateFromStringView) {
  cpp17::string_view str_view("12345");
  cpp17::string_view v_str(str_view);

  EXPECT_FALSE(v_str.empty());
  EXPECT_EQ(str_view.data(), v_str.data());
  EXPECT_EQ(str_view.length(), v_str.length());
}

TEST(StringViewTest, CreateFromConstexprStringView) {
  constexpr cpp17::string_view kLiteral = "12345";
  cpp17::string_view v_str(kLiteral);

  EXPECT_EQ(kLiteral.data(), v_str.data());
  EXPECT_EQ(kLiteral.length(), v_str.length());
}

TEST(StringViewTest, CreateFromConstexprStringViewConstructor) {
  constexpr cpp17::string_view kLiteral("12345");

  EXPECT_EQ(5u, kLiteral.size());
  EXPECT_EQ(5u, kLiteral.length());
}

TEST(StringViewTest, CreateFromStringViewLiteral) {
  {
    using namespace cpp17::literals;
    constexpr cpp17::string_view kLiteral = "12345"_sv;

    EXPECT_EQ(5u, kLiteral.size());
    EXPECT_EQ(5u, kLiteral.length());

    constexpr cpp17::wstring_view kWLiteral = L"12345"_sv;

    EXPECT_EQ(5u, kWLiteral.size());
    EXPECT_EQ(5u, kWLiteral.length());

    constexpr cpp17::u16string_view ku16Literal = u"12345"_sv;
    EXPECT_EQ(5u, ku16Literal.size());
    EXPECT_EQ(5u, ku16Literal.length());

    constexpr cpp17::u16string_view ku32Literal = u"12345"_sv;
    EXPECT_EQ(5u, ku32Literal.size());
    EXPECT_EQ(5u, ku32Literal.length());
  }
  {
    using namespace cpp17::string_view_literals;
    constexpr cpp17::string_view kLiteral __attribute__((unused)) = "12345"_sv;
    constexpr cpp17::wstring_view kWLiteral __attribute__((unused)) = L"12345"_sv;
    constexpr cpp17::u16string_view ku16Literal __attribute__((unused)) = u"12345"_sv;
    constexpr cpp17::u16string_view ku32Literal __attribute__((unused)) = u"12345"_sv;
  }
  {
    using namespace cpp17::literals::string_view_literals;
    constexpr cpp17::string_view kLiteral __attribute__((unused)) = "12345"_sv;
    constexpr cpp17::wstring_view kWLiteral __attribute__((unused)) = L"12345"_sv;
    constexpr cpp17::u16string_view ku16Literal __attribute__((unused)) = u"12345"_sv;
    constexpr cpp17::u16string_view ku32Literal __attribute__((unused)) = u"12345"_sv;
  }
}

TEST(StringViewTest, SizeIsSameAsLength) {
  constexpr cpp17::string_view kLiteral = "12345";

  EXPECT_EQ(5u, kLiteral.size());
  EXPECT_EQ(5u, kLiteral.length());
}

TEST(StringViewTest, ArrayAccessOperator) {
  // Need static duration to enforce subobject constexpr, otherwise it is not allowed.
  constexpr static char const kLiteral[] = "12345";
  constexpr cpp17::string_view kFitLiteral(kLiteral);

  for (std::size_t i = 0; i < kFitLiteral.size(); ++i) {
    EXPECT_EQ(kLiteral[i], kFitLiteral[i]) << "Array access returned wrong value.";
    EXPECT_EQ(&kLiteral[i], &kFitLiteral[i]) << "Array access returned value at different address.";
  }
}

TEST(StringViewTest, BeginPointsToFirstElement) {
  constexpr cpp17::string_view kLiteral = "12345";

  EXPECT_EQ(&kLiteral[0], &(*kLiteral.begin()));
  EXPECT_EQ(&kLiteral[4], &(*kLiteral.rbegin()));
}

TEST(StringViewTest, EndPointsOnePastLastElement) {
  constexpr cpp17::string_view kLiteral = "12345";

  EXPECT_EQ(&kLiteral[4], &(*(kLiteral.end() - 1)));
  EXPECT_EQ(&kLiteral[0], &(*(kLiteral.rend() - 1)));
}

TEST(StringViewTest, EndPointsPastLastElement) {
  constexpr cpp17::string_view kLiteral = "12345";

  EXPECT_EQ(kLiteral.begin() + 5, kLiteral.end());
  EXPECT_EQ(kLiteral.rbegin() + 5, kLiteral.rend());
}

TEST(StringViewTest, WhenEmptyBeginIsSameAsEnd) {
  constexpr cpp17::string_view kLiteral = "";

  EXPECT_EQ(kLiteral.begin(), kLiteral.end());
  EXPECT_EQ(kLiteral.rbegin(), kLiteral.rend());
}

TEST(StringViewTest, FrontReturnsRefToFirstElement) {
  constexpr cpp17::string_view kLiteral = "12345";

  EXPECT_EQ(&(*kLiteral.begin()), &kLiteral.front());
}

TEST(StringViewTest, BackReturnsRefToLastElement) {
  constexpr cpp17::string_view kLiteral = "12345";

  EXPECT_EQ(&(*(kLiteral.begin() + 4)), &kLiteral.back());
}

TEST(StringViewTest, EmptyIsTrueForEmptyString) {
  constexpr cpp17::string_view kStr;

  ASSERT_TRUE(kStr.empty());
  ASSERT_EQ(0u, kStr.size());
  ASSERT_EQ(0u, kStr.length());
}

TEST(StringViewTest, AtReturnsElementAtIndex) {
  // Need static duration to enforce subobject constexpr, otherwise it is not allowed.
  constexpr static char const kLiteral[] = "12345";
  constexpr cpp17::string_view kFitLiteral(kLiteral);

  for (std::size_t i = 0; i < kFitLiteral.size(); ++i) {
    EXPECT_EQ(kLiteral[i], kFitLiteral.at(i)) << "Array access returned wrong value.";
    EXPECT_EQ(&kLiteral[i], &kFitLiteral.at(i))
        << "Array access returned value at different address.";
  }
}

TEST(StringViewTest, AtThrowsExceptionWhenIndexIsOOR) {
  ASSERT_THROW_OR_ABORT(
      {
        constexpr cpp17::string_view kFitLiteral("12345");
        kFitLiteral.at(1000);
      },
      std::out_of_range);
}

// Even though we use a custom compare implementation, because we lack a constexpr compare
// function, we use this test to verify that the expectations are equivalent.
TEST(StringViewTest, CompareVerification) {
  constexpr cpp17::string_view kStr1 = "1234";

  // Same string
  {
    constexpr cpp17::string_view kStr2 = "1234";
    constexpr cpp17::string_view kStr3 = "01234";
    EXPECT_EQ(0, cpp17::string_view::traits_type::compare(kStr1.data(), kStr2.data(), 4));

    EXPECT_EQ(0, kStr1.compare(kStr2));
    EXPECT_EQ(0, kStr3.compare(1, kStr3.length() - 1, kStr2));
    EXPECT_EQ(0, kStr1.compare(1, kStr1.length() - 2, kStr2, 1, kStr2.length() - 2));

    EXPECT_EQ(0, kStr1.compare("1234"));
    EXPECT_EQ(0, kStr1.compare(1, kStr1.length() - 1, "234"));
    EXPECT_EQ(0, kStr1.compare(2, kStr1.length() - 2, "234", 1, 2));
  }

  // Same Length higher character
  {
    constexpr cpp17::string_view kStr2 = "1235";
    EXPECT_LT(cpp17::string_view::traits_type::compare(kStr1.data(), kStr2.data(), 4), 0);

    EXPECT_LT(kStr1.compare(kStr2), 0);
    EXPECT_LT(kStr1.compare(0, kStr1.length(), kStr2), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 2, kStr2, 1, kStr2.length() - 1), 0);

    EXPECT_LT(kStr1.compare("1235"), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 1, "235"), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 2, "1235", 1, 3), 0);
  }

  // Same Length lower character
  {
    constexpr cpp17::string_view kStr2 = "1232";
    EXPECT_GT(cpp17::string_view::traits_type::compare(kStr1.data(), kStr2.data(), 4), 0);

    EXPECT_GT(kStr1.compare(kStr2), 0);
    EXPECT_GT(kStr2.compare(1, kStr2.length() - 1, kStr1), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 1, kStr2, 1, kStr2.length() - 1), 0);

    EXPECT_GT(kStr1.compare("1232"), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 1, "232"), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 2, "22", 1, kStr2.length() - 2), 0);
  }

  // Greater Length
  {
    constexpr cpp17::string_view kStr2 = "12345";
    constexpr cpp17::string_view kStr3 = "2345";

    EXPECT_LT(kStr1.compare(kStr2), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 1, kStr3), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 1, kStr2, 1, kStr2.length() - 1), 0);

    EXPECT_LT(kStr1.compare(kStr2.data()), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 1, kStr3.data()), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 1, kStr2.data(), 1, kStr2.length() - 1), 0);
  }

  // Shorter Length
  {
    constexpr cpp17::string_view kStr2 = "123";
    constexpr cpp17::string_view kStr3 = "23";

    EXPECT_GT(kStr1.compare(kStr2), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 1, kStr3), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 1, kStr2, 1, kStr2.length() - 1), 0);

    EXPECT_GT(kStr1.compare(kStr2.data()), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 1, kStr3.data()), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 1, kStr2.data(), 1, kStr2.length() - 1), 0);
  }
}

// Check that they calls are equivalent to what the standard expects.
TEST(StringViewTest, CompareOverloadCheck) {
  constexpr cpp17::string_view kString1 = "123";
  constexpr cpp17::string_view kString2 = "1234";

  {
    cpp17::string_view expected = kString1.substr(1, 2);
    EXPECT_EQ(kString1.substr(1, 2).compare(expected), kString1.compare(1, 2, expected));
  }

  {
    EXPECT_EQ(kString1.substr(1, 2).compare(kString2.substr(1, 2)),
              kString1.compare(1, 2, kString2, 1, 2));
  }

  { EXPECT_EQ(kString1.compare(cpp17::string_view("123")), kString1.compare("123")); }

  {
    EXPECT_EQ(kString1.substr(1, 2).compare(cpp17::string_view("123")),
              kString1.compare(1, 2, "123"));
  }

  {
    EXPECT_EQ(kString1.substr(1, 2).compare(cpp17::string_view("123")),
              kString1.compare(1, 2, "123"));
  }
}

TEST(StringViewTest, OperatorEq) {
  constexpr cpp17::string_view kStrView = "Self1234";

  EXPECT_EQ(kStrView, kStrView);
  EXPECT_EQ(kStrView, cpp17::string_view("Self1234"));
  EXPECT_EQ(kStrView, cpp17::string_view("Self12345").substr(0, kStrView.length()));
  EXPECT_EQ(kStrView, "Self1234");
  EXPECT_EQ("Self1234", kStrView);
}

TEST(StringViewTest, OperatorNe) {
  constexpr cpp17::string_view kStrView = "Self1234";

  EXPECT_NE(kStrView, cpp17::string_view());
  EXPECT_NE(kStrView, cpp17::string_view("Self12345"));
  EXPECT_NE(kStrView, "Self12345");
  EXPECT_NE("Self12345", kStrView);
}

TEST(StringViewTest, OperatorLess) {
  constexpr cpp17::string_view kStrView = "Self1234";

  EXPECT_TRUE(kStrView < "Self12345");
  EXPECT_TRUE("Self123" < kStrView);
  EXPECT_TRUE(kStrView < cpp17::string_view("Self12345"));
}

TEST(StringViewTest, OperatorLessOrEq) {
  constexpr cpp17::string_view kStrView = "Self1234";

  EXPECT_TRUE(kStrView <= "Self12345");
  EXPECT_TRUE("Self123" <= kStrView);
  EXPECT_TRUE(kStrView <= cpp17::string_view("Self12345"));
  EXPECT_TRUE(kStrView <= cpp17::string_view("Self1234"));
}

TEST(StringViewTest, OperatorGreater) {
  constexpr cpp17::string_view kStrView = "Self1234";

  EXPECT_TRUE(kStrView > "Self123");
  EXPECT_TRUE("Self12345" > kStrView);
  EXPECT_TRUE(kStrView > cpp17::string_view("Self123"));
}

TEST(StringViewTest, OperatorGreaterOrEq) {
  constexpr cpp17::string_view kStrView = "Self1234";

  EXPECT_TRUE(kStrView >= "Self123");
  EXPECT_TRUE("Self12345" >= kStrView);
  EXPECT_TRUE(kStrView >= cpp17::string_view("Self123"));
  EXPECT_TRUE(kStrView >= cpp17::string_view("Self1234"));
}

TEST(StringViewTest, RemovePrefix) {
  constexpr cpp17::string_view kPrefixWithSuffix = "PrefixSuffix";
  cpp17::string_view str_view = kPrefixWithSuffix;

  str_view.remove_prefix(6);
  EXPECT_EQ(kPrefixWithSuffix.length() - 6, str_view.length());
  auto no_suffix = kPrefixWithSuffix.substr(6, kPrefixWithSuffix.length() - 6);
  EXPECT_EQ(no_suffix, str_view);
  EXPECT_EQ("Suffix", str_view);
}

TEST(StringViewTest, RemoveSuffix) {
  constexpr cpp17::string_view kPrefixWithSuffix = "PrefixSuffix";
  cpp17::string_view str_view = kPrefixWithSuffix;

  str_view.remove_suffix(6);
  EXPECT_EQ(kPrefixWithSuffix.length() - 6, str_view.length());
  auto no_suffix = kPrefixWithSuffix.substr(0, kPrefixWithSuffix.length() - 6);
  EXPECT_EQ(no_suffix, str_view);
  EXPECT_EQ("Prefix", str_view);
}

TEST(StringViewTest, SubstrNoArgsAreEqual) {
  constexpr cpp17::string_view kLiteral = "12345";

  EXPECT_EQ(kLiteral, kLiteral.substr());
}

TEST(StringViewTest, SubstrWithPosIsMatchesSubstring) {
  constexpr cpp17::string_view kLiteral = "12345";
  constexpr cpp17::string_view kExpectedLiteral = "345";

  EXPECT_EQ(kExpectedLiteral, kLiteral.substr(2));
}

TEST(StringViewTest, SubstrWithPosAndCountIsMatchesSubstring) {
  constexpr cpp17::string_view kLiteral = "12345";
  constexpr cpp17::string_view kExpectedLiteral = "34";

  EXPECT_EQ(kExpectedLiteral, kLiteral.substr(2, 2));
}

TEST(StringViewTest, Swap) {
  cpp17::string_view str_1 = "12345";
  cpp17::string_view str_2 = "34";

  str_1.swap(str_2);

  EXPECT_EQ("34", str_1);
  EXPECT_EQ("12345", str_2);
}

TEST(StringViewTest, Copy) {
  constexpr cpp17::string_view kBase = "Base";
  constexpr cpp17::string_view::size_type kSize = 2;
  cpp17::string_view::value_type dest[kSize + 1] = {'\0'};
  cpp17::string_view::value_type dest_traits[kSize + 1] = {'\0'};

  EXPECT_EQ(kSize, kBase.copy(dest, kSize, 0));
  EXPECT_EQ(dest_traits, cpp17::string_view::traits_type::copy(dest_traits, kBase.data(), kSize));
  EXPECT_STREQ(dest_traits, dest);
}

TEST(StringViewTest, CopyThrowsExceptionOnOOR) {
  ASSERT_THROW_OR_ABORT(
      {
        constexpr cpp17::string_view v_str = "Base";
        cpp17::string_view::value_type dest[v_str.length()] = {};
        memset(dest, '\0', v_str.length());

        v_str.copy(dest, v_str.length(), v_str.length() + 1);
      },
      std::out_of_range);
}

TEST(StringViewTest, SubstrThrowsExceptionOnOOR) {
  ASSERT_THROW_OR_ABORT(
      {
        constexpr cpp17::string_view v_str = "Base";
        [[gnu::unused]] auto s_str = v_str.substr(v_str.length() + 1);
      },
      std::out_of_range);
}

TEST(StringViewTest, MaxSizeIsMaxAddressableSize) {
  cpp17::string_view str_view("12345");

  EXPECT_EQ(std::numeric_limits<cpp17::string_view::size_type>::max(), str_view.max_size());
}

TEST(StringViewTest, FindReturnsFirstCharTypeMatch) {
  constexpr cpp17::string_view kString = "12345678901234567890";

  EXPECT_EQ(0u, kString.find('1'));
  EXPECT_EQ(1u, kString.find('2'));
  EXPECT_EQ(2u, kString.find('3'));
  EXPECT_EQ(3u, kString.find('4'));
  EXPECT_EQ(4u, kString.find('5'));
  EXPECT_EQ(5u, kString.find('6'));
  EXPECT_EQ(6u, kString.find('7'));
  EXPECT_EQ(7u, kString.find('8'));
  EXPECT_EQ(8u, kString.find('9'));
  EXPECT_EQ(9u, kString.find('0'));
}

TEST(StringViewTest, FindWithPosReturnsFirstCharTypeMatch) {
  constexpr cpp17::string_view kString = "12345678901234567890";

  EXPECT_EQ(10u, kString.find('1', 10));
  EXPECT_EQ(11u, kString.find('2', 10));
  EXPECT_EQ(12u, kString.find('3', 10));
  EXPECT_EQ(13u, kString.find('4', 10));
  EXPECT_EQ(14u, kString.find('5', 10));
  EXPECT_EQ(15u, kString.find('6', 10));
  EXPECT_EQ(16u, kString.find('7', 10));
  EXPECT_EQ(17u, kString.find('8', 10));
  EXPECT_EQ(18u, kString.find('9', 10));
  EXPECT_EQ(19u, kString.find('0', 10));
}

TEST(StringViewTest, FindReturnsNposWhenNoCharTypeMatch) {
  constexpr cpp17::string_view kString = "123456789123456789";

  EXPECT_EQ(cpp17::string_view::npos, kString.find('0'));
}

TEST(StringViewTest, FindReturnsFirstMatch) {
  constexpr cpp17::string_view kString = "12345678901234567890";

  EXPECT_EQ(0u, kString.find(""));
  EXPECT_EQ(0u, kString.find("12"));
  EXPECT_EQ(1u, kString.find("23"));
  EXPECT_EQ(2u, kString.find("34"));
  EXPECT_EQ(3u, kString.find("45"));
  EXPECT_EQ(4u, kString.find("56"));
  EXPECT_EQ(5u, kString.find("67"));
  EXPECT_EQ(6u, kString.find("78"));
  EXPECT_EQ(7u, kString.find("89"));
  EXPECT_EQ(8u, kString.find("90"));
  EXPECT_EQ(9u, kString.find("01"));

  EXPECT_EQ(9u, kString.find("01234"));
}

TEST(StringViewTest, FindWithPosReturnsFirstMatch) {
  constexpr cpp17::string_view kString = "12345678901234567890";

  EXPECT_EQ(10u, kString.find("", 10));
  EXPECT_EQ(10u, kString.find("1", 10));
  EXPECT_EQ(11u, kString.find("2", 10));
  EXPECT_EQ(12u, kString.find("3", 10));
  EXPECT_EQ(13u, kString.find("4", 10));
  EXPECT_EQ(14u, kString.find("5", 10));
  EXPECT_EQ(15u, kString.find("6", 10));
  EXPECT_EQ(16u, kString.find("7", 10));
  EXPECT_EQ(17u, kString.find("8", 10));
  EXPECT_EQ(18u, kString.find("9", 10));
  EXPECT_EQ(19u, kString.find("0", 10));

  // String of size > 1.
  EXPECT_EQ(13u, kString.find("456", 10));
}

TEST(StringViewTest, FindReturnsNposWhenNoMatch) {
  constexpr cpp17::string_view kString = "12345678901234567890";

  // String of size > 1.
  EXPECT_EQ(cpp17::string_view::npos, kString.find("A"));
  EXPECT_EQ(cpp17::string_view::npos, kString.find("02"));
  EXPECT_EQ(cpp17::string_view::npos, kString.find("42321"));
}

TEST(StringViewTest, FindReturnsNposWhenNeedleIsBiggerThanHaystack) {
  constexpr cpp17::string_view kString = "123";

  // String of size > 1.
  EXPECT_EQ(cpp17::string_view::npos, kString.find("1234"));
}

TEST(StringViewTest, RfindReturnsFirstCharTypeMatch) {
  constexpr cpp17::string_view kString = "12345678901234567890";

  EXPECT_EQ(10u, kString.rfind('1'));
  EXPECT_EQ(11u, kString.rfind('2'));
  EXPECT_EQ(12u, kString.rfind('3'));
  EXPECT_EQ(13u, kString.rfind('4'));
  EXPECT_EQ(14u, kString.rfind('5'));
  EXPECT_EQ(15u, kString.rfind('6'));
  EXPECT_EQ(16u, kString.rfind('7'));
  EXPECT_EQ(17u, kString.rfind('8'));
  EXPECT_EQ(18u, kString.rfind('9'));
  EXPECT_EQ(19u, kString.rfind('0'));
}

TEST(StringViewTest, RfindWithPosReturnsFirstCharTypeMatch) {
  constexpr cpp17::string_view kString = "12345678901234567890";

  EXPECT_EQ(10u, kString.rfind('1', 10));
  EXPECT_EQ(1u, kString.rfind('2', 10));
  EXPECT_EQ(2u, kString.rfind('3', 10));
  EXPECT_EQ(3u, kString.rfind('4', 10));
  EXPECT_EQ(4u, kString.rfind('5', 10));
  EXPECT_EQ(5u, kString.rfind('6', 10));
  EXPECT_EQ(6u, kString.rfind('7', 10));
  EXPECT_EQ(7u, kString.rfind('8', 10));
  EXPECT_EQ(8u, kString.rfind('9', 10));
  EXPECT_EQ(9u, kString.rfind('0', 10));
}

TEST(StringViewTest, RfindReturnsNposWhenNoCharTypeMatch) {
  constexpr cpp17::string_view kString = "123456789123456789";

  EXPECT_EQ(cpp17::string_view::npos, kString.rfind('0'));
}

TEST(StringViewTest, RfindReturnsFirstMatch) {
  constexpr cpp17::string_view kString = "12345678901234567890";

  EXPECT_EQ(20u, kString.rfind(""));
  EXPECT_EQ(10u, kString.rfind("12"));
  EXPECT_EQ(11u, kString.rfind("23"));
  EXPECT_EQ(12u, kString.rfind("34"));
  EXPECT_EQ(13u, kString.rfind("45"));
  EXPECT_EQ(14u, kString.rfind("56"));
  EXPECT_EQ(15u, kString.rfind("67"));
  EXPECT_EQ(16u, kString.rfind("78"));
  EXPECT_EQ(17u, kString.rfind("89"));
  EXPECT_EQ(18u, kString.rfind("90"));
  EXPECT_EQ(9u, kString.rfind("01"));

  EXPECT_EQ(9u, kString.rfind("01234"));
}

TEST(StringViewTest, RfindWithPosReturnsFirstMatch) {
  constexpr cpp17::string_view kString = "12345678901234567890";

  EXPECT_EQ(10u, kString.rfind("", 10));
  EXPECT_EQ(10u, kString.rfind("1", 10));
  EXPECT_EQ(1u, kString.rfind("2", 10));
  EXPECT_EQ(2u, kString.rfind("3", 10));
  EXPECT_EQ(3u, kString.rfind("4", 10));
  EXPECT_EQ(4u, kString.rfind("5", 10));
  EXPECT_EQ(5u, kString.rfind("6", 10));
  EXPECT_EQ(6u, kString.rfind("7", 10));
  EXPECT_EQ(7u, kString.rfind("8", 10));
  EXPECT_EQ(8u, kString.rfind("9", 10));
  EXPECT_EQ(9u, kString.rfind("0", 10));

  // String of size > 1.
  EXPECT_EQ(3u, kString.rfind("456", 10));
}

TEST(StringViewTest, RfindReturnsNposWhenNoMatch) {
  constexpr cpp17::string_view kString = "12345678901234567890";

  EXPECT_EQ(cpp17::string_view::npos, kString.rfind("A"));
  EXPECT_EQ(cpp17::string_view::npos, kString.rfind("02"));
  EXPECT_EQ(cpp17::string_view::npos, kString.rfind("42321"));
  EXPECT_EQ(cpp17::string_view::npos, kString.rfind('A'));
}

TEST(StringViewTest, RfindReturnsNposWhenNeedleIsBiggerThanHaystack) {
  constexpr cpp17::string_view kString = "123";

  // String of size > 1.
  EXPECT_EQ(cpp17::string_view::npos, kString.rfind("1234"));
  EXPECT_EQ(cpp17::string_view::npos, cpp17::string_view().find('1'));
}

TEST(StringViewTest, FindFirstOfReturnsFirstMatch) {
  constexpr cpp17::string_view kString = "ABCDE1234ABCDE1234";
  constexpr cpp17::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(5u, kString.find_first_of("321"));
  EXPECT_EQ(5u, kString.find_first_of("123"));
  EXPECT_EQ(5u, kString.find_first_of("231"));
  EXPECT_EQ(5u, kString.find_first_of("213"));

  EXPECT_EQ(5u, kString.find_first_of(kMatchers));
  EXPECT_EQ(6u, kString.find_first_of('2'));
}

TEST(StringViewTest, FindFirstOfWithPosReturnsFirstMatch) {
  constexpr cpp17::string_view kString = "ABCDE1234ABCDE1234";
  constexpr cpp17::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(14u, kString.find_first_of("321", 9));
  EXPECT_EQ(14u, kString.find_first_of("123", 9));
  EXPECT_EQ(14u, kString.find_first_of("231", 9));
  EXPECT_EQ(14u, kString.find_first_of("213", 9));

  EXPECT_EQ(14u, kString.find_first_of(kMatchers, 9));
  EXPECT_EQ(5u, kString.find_first_of('1'));
}

TEST(StringViewTest, FindFirstOfWithPosAndCountReturnsFirstMatch) {
  constexpr cpp17::string_view kString = "ABCDE1234ABCDE1234";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(14u, kString.find_first_of("123", 9, 1));
  EXPECT_EQ(15u, kString.find_first_of("231", 9, 1));
  EXPECT_EQ(15u, kString.find_first_of("213", 9, 1));
  EXPECT_EQ(16u, kString.find_first_of("321", 9, 1));
}

TEST(StringViewTest, FindFirstOfReturnsNposWhenNoMatch) {
  constexpr cpp17::string_view kString = "ABCDE1234ABCDE1234";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(cpp17::string_view::npos, kString.find_first_of("GHIJK"));
  EXPECT_EQ(cpp17::string_view::npos, kString.find_first_of("G"));
  EXPECT_EQ(cpp17::string_view::npos, kString.find_first_of('G'));
}

TEST(StringViewTest, FindLastOfReturnsLastMatch) {
  constexpr cpp17::string_view kString = "ABCDE1234ABCDE1234F";
  constexpr cpp17::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change last match.
  EXPECT_EQ(16u, kString.find_last_of("321"));
  EXPECT_EQ(16u, kString.find_last_of("123"));
  EXPECT_EQ(16u, kString.find_last_of("231"));
  EXPECT_EQ(16u, kString.find_last_of("213"));
  EXPECT_EQ(18u, kString.find_last_of("F"));

  EXPECT_EQ(16u, kString.find_last_of(kMatchers));
  EXPECT_EQ(15u, kString.find_last_of('2'));
  EXPECT_EQ(18u, kString.find_last_of('F'));
}

TEST(StringViewTest, FindLastOfWithPosReturnsLastMatch) {
  constexpr cpp17::string_view kString = "ABCDE1234ABCDE1234F";
  constexpr cpp17::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change last match.
  EXPECT_EQ(7u, kString.find_last_of("321", 9));
  EXPECT_EQ(7u, kString.find_last_of("123", 9));
  EXPECT_EQ(7u, kString.find_last_of("231", 9));
  EXPECT_EQ(7u, kString.find_last_of("213", 9));

  EXPECT_EQ(0u, kString.find_last_of("A", 0));
  EXPECT_EQ(18u, kString.find_last_of("F", kString.length() + 1));

  EXPECT_EQ(7u, kString.find_last_of(kMatchers, 9));
  EXPECT_EQ(5u, kString.find_last_of('1', 9));
}

TEST(StringViewTest, FindLastOfWithPosAndCountReturnsLastMatch) {
  constexpr cpp17::string_view kString = "ABCDE1234ABCDE1234";

  // Verify that order of chartacters in |s| does not change last match.
  EXPECT_EQ(5u, kString.find_last_of("123", 9, 1));
  EXPECT_EQ(6u, kString.find_last_of("231", 9, 1));
  EXPECT_EQ(6u, kString.find_last_of("213", 9, 1));
  EXPECT_EQ(7u, kString.find_last_of("321", 9, 1));
}

TEST(StringViewTest, FindLastOfReturnsNposWhenNoMatch) {
  constexpr cpp17::string_view kString = "ABCDE1234ABCDE1234";

  // Verify that order of chartacters in |s| does not change last match.
  EXPECT_EQ(cpp17::string_view::npos, kString.find_last_of("GHIJK"));
  EXPECT_EQ(cpp17::string_view::npos, kString.find_last_of("G"));
  EXPECT_EQ(cpp17::string_view::npos, kString.find_last_of('G'));
  EXPECT_EQ(cpp17::string_view::npos, kString.find_last_of('H', 0));
}

TEST(StringViewTest, FindFirstNotOfReturnsFirstNonMatch) {
  constexpr cpp17::string_view kString = "123ABC123";
  constexpr cpp17::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(0u, kString.find_first_not_of(""));
  EXPECT_EQ(3u, kString.find_first_not_of("321"));
  EXPECT_EQ(3u, kString.find_first_not_of("123"));
  EXPECT_EQ(3u, kString.find_first_not_of("231"));
  EXPECT_EQ(3u, kString.find_first_not_of("213"));

  EXPECT_EQ(3u, kString.find_first_not_of(kMatchers));
  EXPECT_EQ(1u, kString.find_first_not_of('1'));
}

TEST(StringViewTest, FindFirstNotOfWithPosReturnsFirstNonMatch) {
  constexpr cpp17::string_view kString = "123ABC123A";
  constexpr cpp17::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(6u, kString.find_first_not_of("", 6));
  EXPECT_EQ(9u, kString.find_first_not_of("321", 6));
  EXPECT_EQ(9u, kString.find_first_not_of("123", 6));
  EXPECT_EQ(9u, kString.find_first_not_of("231", 6));
  EXPECT_EQ(9u, kString.find_first_not_of("213", 6));

  EXPECT_EQ(9u, kString.find_first_not_of(kMatchers, 9));
  EXPECT_EQ(7u, kString.find_first_not_of('1', 6));
}

TEST(StringViewTest, FindFirstNotOfWithPosAndCountReturnsFirstNonMatch) {
  constexpr cpp17::string_view kString = "123ABC123A";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(7u, kString.find_first_not_of("123", 6, 1));
  EXPECT_EQ(6u, kString.find_first_not_of("231", 6, 1));
  EXPECT_EQ(6u, kString.find_first_not_of("213", 6, 1));
  EXPECT_EQ(6u, kString.find_first_not_of("321", 6, 1));
}

TEST(StringViewTest, FindFirstNotOfReturnsNposWhenNoMatch) {
  constexpr cpp17::string_view kString = "GGGGGGGGGGGGG";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(cpp17::string_view::npos, kString.find_first_not_of("ABCG"));
  EXPECT_EQ(cpp17::string_view::npos, kString.find_first_not_of("G"));
  EXPECT_EQ(cpp17::string_view::npos, kString.find_first_not_of('G'));
}

TEST(StringViewTest, FindLastNotOfReturnsLastMatch) {
  constexpr cpp17::string_view kString = "ABCDE1234ABCDE1234";
  constexpr cpp17::string_view kMatchers = "1234";

  // Verify that order of chartacters in |s| does not change last_not match.
  EXPECT_EQ(13u, kString.find_last_not_of("3214"));
  EXPECT_EQ(13u, kString.find_last_not_of("1234"));
  EXPECT_EQ(13u, kString.find_last_not_of("2314"));
  EXPECT_EQ(13u, kString.find_last_not_of("2134"));

  EXPECT_EQ(13u, kString.find_last_not_of(kMatchers));
  EXPECT_EQ(16u, kString.find_last_not_of('4'));
}

TEST(StringViewTest, FindLastNotOfWithPosReturnsLastMatch) {
  constexpr cpp17::string_view kString = "ABCDE1234ABCDE1234";
  constexpr cpp17::string_view kMatchers = "1234";

  // Verify that order of chartacters in |s| does not change last_not match.
  EXPECT_EQ(4u, kString.find_last_not_of("3214", 8));
  EXPECT_EQ(4u, kString.find_last_not_of("1234", 8));
  EXPECT_EQ(4u, kString.find_last_not_of("2314", 8));
  EXPECT_EQ(4u, kString.find_last_not_of("2134", 8));

  EXPECT_EQ(4u, kString.find_last_not_of(kMatchers, 8));
  EXPECT_EQ(7u, kString.find_last_not_of('4', 8));
}

TEST(StringViewTest, FindLastNotOfWithPosAndCountReturnsLastMatch) {
  constexpr cpp17::string_view kString = "ABCDE1234ABCDE1234";

  // Verify that order of chartacters in |s| does not change last_not match.
  EXPECT_EQ(8u, kString.find_last_not_of("1234", 8, 1));
  EXPECT_EQ(8u, kString.find_last_not_of("2314", 8, 2));
  EXPECT_EQ(5u, kString.find_last_not_of("4321", 8, 3));
  EXPECT_EQ(4u, kString.find_last_not_of("3214", 8, 4));
}

TEST(StringViewTest, FindLastNotOfReturnsNposWhenNoMatch) {
  constexpr cpp17::string_view kString = "GGGGGGG";

  // Verify that order of chartacters in |s| does not change last_not match.
  EXPECT_EQ(cpp17::string_view::npos, kString.find_last_not_of("GHIJK"));
  EXPECT_EQ(cpp17::string_view::npos, kString.find_last_not_of("G"));
  EXPECT_EQ(cpp17::string_view::npos, kString.find_last_not_of('G'));
}

TEST(StringViewTest, StartsWith) {
  constexpr cpp17::string_view kString = "ABCdef";

  // By convention, a string view always "starts" with an empty or NUL string.
  EXPECT_TRUE(cpp20::starts_with(kString, cpp17::string_view{}));
  EXPECT_TRUE(cpp20::starts_with(kString, ""));
  EXPECT_TRUE(cpp20::starts_with(kString, cpp17::string_view{""}));

  EXPECT_TRUE(cpp20::starts_with(kString, 'A'));
  EXPECT_FALSE(cpp20::starts_with(kString, 'B'));
  EXPECT_FALSE(cpp20::starts_with(kString, 'f'));

  EXPECT_TRUE(cpp20::starts_with(kString, "A"));
  EXPECT_TRUE(cpp20::starts_with(kString, cpp17::string_view{"A"}));
  EXPECT_TRUE(cpp20::starts_with(kString, "AB"));
  EXPECT_TRUE(cpp20::starts_with(kString, cpp17::string_view{"AB"}));
  EXPECT_TRUE(cpp20::starts_with(kString, "ABC"));
  EXPECT_TRUE(cpp20::starts_with(kString, cpp17::string_view{"ABC"}));
  EXPECT_TRUE(cpp20::starts_with(kString, "ABCd"));
  EXPECT_TRUE(cpp20::starts_with(kString, cpp17::string_view{"ABCd"}));
  EXPECT_TRUE(cpp20::starts_with(kString, "ABCde"));
  EXPECT_TRUE(cpp20::starts_with(kString, cpp17::string_view{"ABCde"}));

  // A string view should start with itself.
  EXPECT_TRUE(cpp20::starts_with(kString, "ABCdef"));
  EXPECT_TRUE(cpp20::starts_with(kString, kString));
  EXPECT_TRUE(cpp20::starts_with(kString, "ABCdef\0"));
  EXPECT_TRUE(cpp20::starts_with(kString, cpp17::string_view{"ABCdef\0"}));

  EXPECT_FALSE(cpp20::starts_with(kString, "rAnDoM"));
  EXPECT_FALSE(cpp20::starts_with(kString, cpp17::string_view{"rAnDoM"}));
  EXPECT_FALSE(cpp20::starts_with(kString, "longer than kString"));
  EXPECT_FALSE(cpp20::starts_with(kString, cpp17::string_view{"longer than kString"}));
}

TEST(StringViewTest, EndsWith) {
  constexpr cpp17::string_view kString = "ABCdef";

  // By convention, a string view always "ends" with an empty or NUL string.
  EXPECT_TRUE(cpp20::ends_with(kString, cpp17::string_view{}));
  EXPECT_TRUE(cpp20::ends_with(kString, ""));
  EXPECT_TRUE(cpp20::ends_with(kString, cpp17::string_view{""}));

  EXPECT_TRUE(cpp20::ends_with(kString, 'f'));
  EXPECT_FALSE(cpp20::ends_with(kString, 'e'));
  EXPECT_FALSE(cpp20::ends_with(kString, 'A'));

  EXPECT_TRUE(cpp20::ends_with(kString, "f"));
  EXPECT_TRUE(cpp20::ends_with(kString, cpp17::string_view{"f"}));
  EXPECT_TRUE(cpp20::ends_with(kString, "ef"));
  EXPECT_TRUE(cpp20::ends_with(kString, cpp17::string_view{"ef"}));
  EXPECT_TRUE(cpp20::ends_with(kString, "def"));
  EXPECT_TRUE(cpp20::ends_with(kString, cpp17::string_view{"def"}));
  EXPECT_TRUE(cpp20::ends_with(kString, "Cdef"));
  EXPECT_TRUE(cpp20::ends_with(kString, cpp17::string_view{"Cdef"}));
  EXPECT_TRUE(cpp20::ends_with(kString, "BCdef"));
  EXPECT_TRUE(cpp20::ends_with(kString, cpp17::string_view{"BCdef"}));

  // A string view should end with itself.
  EXPECT_TRUE(cpp20::ends_with(kString, "ABCdef"));
  EXPECT_TRUE(cpp20::ends_with(kString, kString));
  EXPECT_TRUE(cpp20::ends_with(kString, "ABCdef\0"));
  EXPECT_TRUE(cpp20::ends_with(kString, cpp17::string_view{"ABCdef\0"}));

  EXPECT_FALSE(cpp20::ends_with(kString, "rAnDoM"));
  EXPECT_FALSE(cpp20::ends_with(kString, cpp17::string_view{"rAnDoM"}));
  EXPECT_FALSE(cpp20::ends_with(kString, "longer than kString"));
  EXPECT_FALSE(cpp20::ends_with(kString, cpp17::string_view{"longer than kString"}));
}

TEST(StringViewTest, HashVaidation) {
  constexpr cpp17::string_view kStringView1 = "1234";
  constexpr cpp17::string_view kStringView2 = "123456";
  const std::string kString = "123";

  // Hash of the full view matches the hash of an equivalent string.
  EXPECT_EQ(std::hash<std::string>()(std::string(kStringView1.data())),
            std::hash<cpp17::string_view>()(kStringView1));
  EXPECT_EQ(std::hash<std::string>()(std::string(kStringView2.data())),
            std::hash<cpp17::string_view>()(kStringView2));

  // Hash uses the view, not the full string.
  EXPECT_EQ(std::hash<cpp17::string_view>()(kStringView1),
            std::hash<cpp17::string_view>()(kStringView2.substr(0, kStringView1.length())));

  // Hash matches the hash for a string with the same content.
  EXPECT_EQ(std::hash<std::string>()(kString),
            std::hash<cpp17::string_view>()(kStringView2.substr(0, kString.length())));
  EXPECT_EQ(std::hash<std::string>()(kString),
            std::hash<cpp17::string_view>()(kStringView1.substr(0, kString.length())));

  // If the hash of the contents are different in the default hash of the string, they should be
  // different in the specialized hash. We should make no assumptions on whether the values will hit
  // the same bucket or not.
  EXPECT_EQ(std::hash<std::string>()(std::string(kStringView1.data())) !=
                std::hash<std::string>()(std::string(kStringView2.data())),
            std::hash<cpp17::string_view>()(kStringView1) !=
                std::hash<cpp17::string_view>()(kStringView2));
}

TEST(StringViewTest, OutputStreamOperatorFitsWithinWidth) {
  constexpr cpp17::string_view kStringView = "1234";
  std::ostringstream oss;
  oss.width(kStringView.length());

  oss << kStringView;

  EXPECT_EQ(oss.str(), kStringView);
}

TEST(StringViewTest, OutputStreamOperatorExpandsToStringViewWidth) {
  constexpr cpp17::string_view kStringView = "1234";
  std::ostringstream oss;
  oss.width(kStringView.length() - 1);

  oss << kStringView;

  EXPECT_EQ(oss.str(), kStringView);
  EXPECT_EQ(oss.width(), 0);
}

TEST(StringViewTest, OutputStreamOperatorFillsExtraSpaceToTheRight) {
  constexpr cpp17::string_view kStringView = "1234";
  constexpr cpp17::string_view kExpected = "000001234";
  std::ostringstream oss;
  oss.fill('0');
  oss.width(kExpected.length());

  oss << kStringView;

  EXPECT_EQ(oss.str(), kExpected);
}

TEST(StringViewTest, OutputStreamOperatorFillsExtraSpaceToTheLeft) {
  constexpr cpp17::string_view kStringView = "1234";
  constexpr cpp17::string_view kExpected = "123400000";
  std::ostringstream oss;
  oss.fill('0');
  oss.setf(std::ios_base::left);
  oss.width(kExpected.length());

  oss << kStringView;

  EXPECT_EQ(oss.str(), kExpected);
}

TEST(StringViewTest, OutputStreamOperatorResetsWidthToZero) {
  constexpr cpp17::string_view kStringView = "1234";
  std::ostringstream oss;
  oss.fill('0');
  oss.width(10);

  oss << kStringView;

  EXPECT_EQ(0, oss.width());
}

TEST(StringViewTest, BracketOperatorAssersOnUB) {
  DEBUG_ASSERT_DEATH({
    constexpr cpp17::string_view kView = "1234";
    kView[kView.size()];
  });
}

TEST(StringViewTest, RemovePrefixAssertsOnUB) {
  DEBUG_ASSERT_DEATH({
    cpp17::string_view kView = "1234";
    kView.remove_prefix(kView.size() + 1);
  });
}

TEST(StringViewTest, RemoveSuffixAssertOnUB) {
  DEBUG_ASSERT_DEATH({
    cpp17::string_view kView = "1234";
    kView.remove_suffix(kView.size() + 1);
  });
}

}  // namespace
