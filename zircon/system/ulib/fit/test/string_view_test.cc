// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>

#include <lib/fit/string_view.h>

#include <cstring>
#include <limits>
#include <sstream>

#include <zxtest/zxtest.h>

namespace fit {
namespace {

TEST(StringViewTest, CreateFromStdString) {
  std::string str = "1";
  fit::string_view v_str(str);

  EXPECT_FALSE(v_str.empty());
  EXPECT_EQ(str.data(), v_str.data());
  EXPECT_EQ(str.length(), v_str.length());
}

TEST(StringViewTest, CreateFromCArray) {
  constexpr char kStr[] = "1";
  fit::string_view v_str(kStr);

  EXPECT_FALSE(v_str.empty());
  EXPECT_EQ(kStr, v_str.data());
  EXPECT_EQ(std::strlen(kStr), v_str.length());
}

TEST(StringViewTest, CreateFromConstChar) {
  const char* kStr = "1";
  fit::string_view v_str(kStr);

  EXPECT_FALSE(v_str.empty());
  EXPECT_EQ(kStr, v_str.data());
  EXPECT_EQ(std::strlen(kStr), v_str.length());
}

TEST(StringViewTest, CreateFromStringView) {
  fit::string_view str_view("12345");
  fit::string_view v_str(str_view);

  EXPECT_FALSE(v_str.empty());
  EXPECT_EQ(str_view.data(), v_str.data());
  EXPECT_EQ(str_view.length(), v_str.length());
}

TEST(StringViewTest, CreateFromConstexprStringView) {
  constexpr fit::string_view kLiteral = "12345";
  fit::string_view v_str(kLiteral);

  EXPECT_EQ(kLiteral.data(), v_str.data());
  EXPECT_EQ(kLiteral.length(), v_str.length());
}

TEST(StringViewTest, CreateFromConstexprStringViewConstructor) {
  constexpr fit::string_view kLiteral("12345");

  EXPECT_EQ(5, kLiteral.size());
  EXPECT_EQ(5, kLiteral.length());
}

TEST(StringViewTest, CreateFromStringViewLiteral) {
  constexpr fit::string_view kLiteral = "12345"_sv;

  EXPECT_EQ(5, kLiteral.size());
  EXPECT_EQ(5, kLiteral.length());
}

TEST(StringViewTest, SizeIsSameAsLength) {
  constexpr fit::string_view kLiteral = "12345";

  EXPECT_EQ(5, kLiteral.size());
  EXPECT_EQ(5, kLiteral.length());
}

TEST(StringViewTest, ArrayAccessOperator) {
  // Need static duration to enforce subobject constexpr, otherwise it is not allowed.
  constexpr static char const kLiteral[] = "12345";
  constexpr fit::string_view kFitLiteral(kLiteral);

  for (std::size_t i = 0; i < kFitLiteral.size(); ++i) {
    EXPECT_EQ(kLiteral[i], kFitLiteral[i], "Array access returned wrong value.");
    EXPECT_EQ(&kLiteral[i], &kFitLiteral[i], "Array access returned value at different address.");
  }
}

TEST(StringViewTest, BeginPointsToFirstElement) {
  constexpr fit::string_view kLiteral = "12345";

  EXPECT_EQ(&kLiteral[0], &(*kLiteral.begin()));
  EXPECT_EQ(&kLiteral[4], &(*kLiteral.rbegin()));
}

TEST(StringViewTest, EndPointsOnePastLastElement) {
  constexpr fit::string_view kLiteral = "12345";

  EXPECT_EQ(&kLiteral[4], &(*(kLiteral.end() - 1)));
  EXPECT_EQ(&kLiteral[0], &(*(kLiteral.rend() - 1)));
}

TEST(StringViewTest, EndPointsPastLastElement) {
  constexpr fit::string_view kLiteral = "12345";

  EXPECT_EQ(kLiteral.begin() + 5, kLiteral.end());
  EXPECT_EQ(kLiteral.rbegin() + 5, kLiteral.rend());
}

TEST(StringViewTest, WhenEmptyBeginIsSameAsEnd) {
  constexpr fit::string_view kLiteral = "";

  EXPECT_EQ(kLiteral.begin(), kLiteral.end());
  EXPECT_EQ(kLiteral.rbegin(), kLiteral.rend());
}

TEST(StringViewTest, FrontReturnsRefToFirstElement) {
  constexpr fit::string_view kLiteral = "12345";

  EXPECT_EQ(&(*kLiteral.begin()), &kLiteral.front());
}

TEST(StringViewTest, BackReturnsRefToLastElement) {
  constexpr fit::string_view kLiteral = "12345";

  EXPECT_EQ(&(*(kLiteral.begin() + 4)), &kLiteral.back());
}

TEST(StringViewTest, EmptyIsTrueForEmptyString) {
  constexpr fit::string_view kStr;

  ASSERT_TRUE(kStr.empty());
  ASSERT_EQ(0, kStr.size());
  ASSERT_EQ(0, kStr.length());
}

TEST(StringViewTest, AtReturnsElementAtIndex) {
  // Need static duration to enforce subobject constexpr, otherwise it is not allowed.
  constexpr static char const kLiteral[] = "12345";
  constexpr fit::string_view kFitLiteral(kLiteral);

  for (std::size_t i = 0; i < kFitLiteral.size(); ++i) {
    EXPECT_EQ(kLiteral[i], kFitLiteral.at(i), "Array access returned wrong value.");
    EXPECT_EQ(&kLiteral[i], &kFitLiteral.at(i),
              "Array access returned value at different address.");
  }
}

#if !defined(NDEBUG)
TEST(StringViewTest, AtThrowsExceptionWhenIndexIsOOR) {
  ASSERT_DEATH([] {
    constexpr fit::string_view kFitLiteral("12345");
    kFitLiteral.at(5);
  });
}
#endif

// Even though we use a custom compare implementation, because we lack a constexpr compare
// function, we use this test to verify that the expectations are equivalent.
TEST(StringViewTest, CompareVerification) {
  constexpr fit::string_view kStr1 = "1234";

  // Same string
  {
    constexpr fit::string_view kStr2 = "1234";
    constexpr fit::string_view kStr3 = "01234";
    EXPECT_EQ(0, fit::string_view::traits_type::compare(kStr1.data(), kStr2.data(), 4));

    EXPECT_EQ(0, kStr1.compare(kStr2));
    EXPECT_EQ(0, kStr3.compare(1, kStr3.length() - 1, kStr2));
    EXPECT_EQ(0, kStr1.compare(1, kStr1.length() - 2, kStr2, 1, kStr2.length() - 2));

    EXPECT_EQ(0, kStr1.compare("1234"));
    EXPECT_EQ(0, kStr1.compare(1, kStr1.length() - 1, "234"));
    EXPECT_EQ(0, kStr1.compare(2, kStr1.length() - 2, "234", 1, 2));
  }

  // Same Length higher character
  {
    constexpr fit::string_view kStr2 = "1235";
    EXPECT_LT(fit::string_view::traits_type::compare(kStr1.data(), kStr2.data(), 4), 0);

    EXPECT_LT(kStr1.compare(kStr2), 0);
    EXPECT_LT(kStr1.compare(0, kStr1.length(), kStr2), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 2, kStr2, 1, kStr2.length() - 1), 0);

    EXPECT_LT(kStr1.compare("1235"), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 1, "235"), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 2, "1235", 1, 3), 0);
  }

  // Same Length lower character
  {
    constexpr fit::string_view kStr2 = "1232";
    EXPECT_GT(fit::string_view::traits_type::compare(kStr1.data(), kStr2.data(), 4), 0);

    EXPECT_GT(kStr1.compare(kStr2), 0);
    EXPECT_GT(kStr2.compare(1, kStr2.length() - 1, kStr1), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 1, kStr2, 1, kStr2.length() - 1), 0);

    EXPECT_GT(kStr1.compare("1232"), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 1, "232"), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 2, "22", 1, kStr2.length() - 2), 0);
  }

  // Greater Length
  {
    constexpr fit::string_view kStr2 = "12345";
    constexpr fit::string_view kStr3 = "2345";

    EXPECT_LT(kStr1.compare(kStr2), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 1, kStr3), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 1, kStr2, 1, kStr2.length() - 1), 0);

    EXPECT_LT(kStr1.compare(kStr2.data()), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 1, kStr3.data()), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 1, kStr2.data(), 1, kStr2.length() - 1), 0);
  }

  // Shorter Length
  {
    constexpr fit::string_view kStr2 = "123";
    constexpr fit::string_view kStr3 = "23";

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
  constexpr fit::string_view kString1 = "123";
  constexpr fit::string_view kString2 = "1234";

  {
    fit::string_view expected = kString1.substr(1, 2);
    EXPECT_EQ(kString1.substr(1, 2).compare(expected), kString1.compare(1, 2, expected));
  }

  {
    EXPECT_EQ(kString1.substr(1, 2).compare(kString2.substr(1, 2)),
              kString1.compare(1, 2, kString2, 1, 2));
  }

  { EXPECT_EQ(kString1.compare(fit::string_view("123")), kString1.compare("123")); }

  {
    EXPECT_EQ(kString1.substr(1, 2).compare(fit::string_view("123")),
              kString1.compare(1, 2, "123"));
  }

  {
    EXPECT_EQ(kString1.substr(1, 2).compare(fit::string_view("123")),
              kString1.compare(1, 2, "123"));
  }
}

TEST(StringViewTest, OperatorEq) {
  constexpr fit::string_view kStrView = "Self1234";

  EXPECT_EQ(kStrView, kStrView);
  EXPECT_EQ(kStrView, fit::string_view("Self1234"));
  EXPECT_EQ(kStrView, fit::string_view("Self12345").substr(0, kStrView.length()));
  EXPECT_EQ(kStrView, "Self1234");
  EXPECT_EQ("Self1234", kStrView);
}

TEST(StringViewTest, OperatorNe) {
  constexpr fit::string_view kStrView = "Self1234";

  EXPECT_NE(kStrView, fit::string_view());
  EXPECT_NE(kStrView, fit::string_view("Self12345"));
  EXPECT_NE(kStrView, "Self12345");
  EXPECT_NE("Self12345", kStrView);
}

TEST(StringViewTest, OperatorLess) {
  constexpr fit::string_view kStrView = "Self1234";

  EXPECT_TRUE(kStrView < "Self12345");
  EXPECT_TRUE("Self123" < kStrView);
  EXPECT_TRUE(kStrView < fit::string_view("Self12345"));
}

TEST(StringViewTest, OperatorLessOrEq) {
  constexpr fit::string_view kStrView = "Self1234";

  EXPECT_TRUE(kStrView <= "Self12345");
  EXPECT_TRUE("Self123" <= kStrView);
  EXPECT_TRUE(kStrView <= fit::string_view("Self12345"));
  EXPECT_TRUE(kStrView <= fit::string_view("Self1234"));
}

TEST(StringViewTest, OperatorGreater) {
  constexpr fit::string_view kStrView = "Self1234";

  EXPECT_TRUE(kStrView > "Self123");
  EXPECT_TRUE("Self12345" > kStrView);
  EXPECT_TRUE(kStrView > fit::string_view("Self123"));
}

TEST(StringViewTest, OperatorGreaterOrEq) {
  constexpr fit::string_view kStrView = "Self1234";

  EXPECT_TRUE(kStrView >= "Self123");
  EXPECT_TRUE("Self12345" >= kStrView);
  EXPECT_TRUE(kStrView >= fit::string_view("Self123"));
  EXPECT_TRUE(kStrView >= fit::string_view("Self1234"));
}

TEST(StringViewTest, RemovePrefix) {
  constexpr fit::string_view kPrefixWithSuffix = "PrefixSuffix";
  fit::string_view str_view = kPrefixWithSuffix;

  str_view.remove_prefix(6);
  EXPECT_EQ(kPrefixWithSuffix.length() - 6, str_view.length());
  auto no_suffix = kPrefixWithSuffix.substr(6, kPrefixWithSuffix.length() - 6);
  EXPECT_EQ(no_suffix, str_view);
  EXPECT_EQ("Suffix", str_view);
}

TEST(StringViewTest, RemoveSuffix) {
  constexpr fit::string_view kPrefixWithSuffix = "PrefixSuffix";
  fit::string_view str_view = kPrefixWithSuffix;

  str_view.remove_suffix(6);
  EXPECT_EQ(kPrefixWithSuffix.length() - 6, str_view.length());
  auto no_suffix = kPrefixWithSuffix.substr(0, kPrefixWithSuffix.length() - 6);
  EXPECT_EQ(no_suffix, str_view);
  EXPECT_EQ("Prefix", str_view);
}

TEST(StringViewTest, SubstrNoArgsAreEqual) {
  constexpr fit::string_view kLiteral = "12345";

  EXPECT_EQ(kLiteral, kLiteral.substr());
}

TEST(StringViewTest, SubstrWithPosIsMatchesSubstring) {
  constexpr fit::string_view kLiteral = "12345";
  constexpr fit::string_view kExpectedLiteral = "345";

  EXPECT_EQ(kExpectedLiteral, kLiteral.substr(2));
}

TEST(StringViewTest, SubstrWithPosAndCountIsMatchesSubstring) {
  constexpr fit::string_view kLiteral = "12345";
  constexpr fit::string_view kExpectedLiteral = "34";

  EXPECT_EQ(kExpectedLiteral, kLiteral.substr(2, 2));
}

TEST(StringViewTest, Swap) {
  fit::string_view str_1 = "12345";
  fit::string_view str_2 = "34";

  str_1.swap(str_2);

  EXPECT_EQ("34", str_1);
  EXPECT_EQ("12345", str_2);
}

TEST(StringViewTest, Copy) {
  constexpr fit::string_view kBase = "Base";
  constexpr fit::string_view::size_type kSize = 2;
  fit::string_view::value_type dest[kSize + 1] = {'\0'};
  fit::string_view::value_type dest_traits[kSize + 1] = {'\0'};

  EXPECT_EQ(kSize, kBase.copy(dest, kSize, 0));
  EXPECT_EQ(dest_traits, fit::string_view::traits_type::copy(dest_traits, kBase.data(), kSize));
  EXPECT_STR_EQ(dest_traits, dest);
}

#if !defined(NDEBUG)
TEST(StringViewTest, CopyThrowsExceptionOnOOR) {
  ASSERT_DEATH([]() {
    constexpr fit::string_view v_str = "Base";
    fit::string_view::value_type dest[v_str.length() + 2] = {};
    memset(dest, '\0', v_str.length() + 1);

    v_str.copy(dest, v_str.length(), v_str.length());
  });
}
#endif

TEST(StringViewTest, MaxSizeIsMaxAddressableSize) {
  fit::string_view str_view("12345");

  EXPECT_EQ(std::numeric_limits<fit::string_view::size_type>::max(), str_view.max_size());
}

TEST(StringViewTest, FindReturnsFirstCharTypeMatch) {
  constexpr fit::string_view kString = "12345678901234567890";

  EXPECT_EQ(0, kString.find('1'));
  EXPECT_EQ(1, kString.find('2'));
  EXPECT_EQ(2, kString.find('3'));
  EXPECT_EQ(3, kString.find('4'));
  EXPECT_EQ(4, kString.find('5'));
  EXPECT_EQ(5, kString.find('6'));
  EXPECT_EQ(6, kString.find('7'));
  EXPECT_EQ(7, kString.find('8'));
  EXPECT_EQ(8, kString.find('9'));
  EXPECT_EQ(9, kString.find('0'));
}

TEST(StringViewTest, FindWithPosReturnsFirstCharTypeMatch) {
  constexpr fit::string_view kString = "12345678901234567890";

  EXPECT_EQ(10, kString.find('1', 10));
  EXPECT_EQ(11, kString.find('2', 10));
  EXPECT_EQ(12, kString.find('3', 10));
  EXPECT_EQ(13, kString.find('4', 10));
  EXPECT_EQ(14, kString.find('5', 10));
  EXPECT_EQ(15, kString.find('6', 10));
  EXPECT_EQ(16, kString.find('7', 10));
  EXPECT_EQ(17, kString.find('8', 10));
  EXPECT_EQ(18, kString.find('9', 10));
  EXPECT_EQ(19, kString.find('0', 10));
}

TEST(StringViewTest, FindReturnsNposWhenNoCharTypeMatch) {
  constexpr fit::string_view kString = "123456789123456789";

  EXPECT_EQ(fit::string_view::npos, kString.find('0'));
}

TEST(StringViewTest, FindReturnsFirstMatch) {
  constexpr fit::string_view kString = "12345678901234567890";

  EXPECT_EQ(0, kString.find(""));
  EXPECT_EQ(0, kString.find("12"));
  EXPECT_EQ(1, kString.find("23"));
  EXPECT_EQ(2, kString.find("34"));
  EXPECT_EQ(3, kString.find("45"));
  EXPECT_EQ(4, kString.find("56"));
  EXPECT_EQ(5, kString.find("67"));
  EXPECT_EQ(6, kString.find("78"));
  EXPECT_EQ(7, kString.find("89"));
  EXPECT_EQ(8, kString.find("90"));
  EXPECT_EQ(9, kString.find("01"));

  EXPECT_EQ(9, kString.find("01234"));
}

TEST(StringViewTest, FindWithPosReturnsFirstMatch) {
  constexpr fit::string_view kString = "12345678901234567890";

  EXPECT_EQ(10, kString.find("", 10));
  EXPECT_EQ(10, kString.find("1", 10));
  EXPECT_EQ(11, kString.find("2", 10));
  EXPECT_EQ(12, kString.find("3", 10));
  EXPECT_EQ(13, kString.find("4", 10));
  EXPECT_EQ(14, kString.find("5", 10));
  EXPECT_EQ(15, kString.find("6", 10));
  EXPECT_EQ(16, kString.find("7", 10));
  EXPECT_EQ(17, kString.find("8", 10));
  EXPECT_EQ(18, kString.find("9", 10));
  EXPECT_EQ(19, kString.find("0", 10));

  // String of size > 1.
  EXPECT_EQ(13, kString.find("456", 10));
}

TEST(StringViewTest, FindReturnsNposWhenNoMatch) {
  constexpr fit::string_view kString = "12345678901234567890";

  // String of size > 1.
  EXPECT_EQ(fit::string_view::npos, kString.find("A"));
  EXPECT_EQ(fit::string_view::npos, kString.find("02"));
  EXPECT_EQ(fit::string_view::npos, kString.find("42321"));
}

TEST(StringViewTest, FindReturnsNposWhenNeedleIsBiggerThanHaystack) {
  constexpr fit::string_view kString = "123";

  // String of size > 1.
  EXPECT_EQ(fit::string_view::npos, kString.find("1234"));
}

TEST(StringViewTest, RrfindReturnsFirstCharTypeMatch) {
  constexpr fit::string_view kString = "12345678901234567890";

  EXPECT_EQ(10, kString.rfind('1'));
  EXPECT_EQ(11, kString.rfind('2'));
  EXPECT_EQ(12, kString.rfind('3'));
  EXPECT_EQ(13, kString.rfind('4'));
  EXPECT_EQ(14, kString.rfind('5'));
  EXPECT_EQ(15, kString.rfind('6'));
  EXPECT_EQ(16, kString.rfind('7'));
  EXPECT_EQ(17, kString.rfind('8'));
  EXPECT_EQ(18, kString.rfind('9'));
  EXPECT_EQ(19, kString.rfind('0'));
}

TEST(StringViewTest, RrfindWithPosReturnsFirstCharTypeMatch) {
  constexpr fit::string_view kString = "12345678901234567890";

  EXPECT_EQ(10, kString.rfind('1', 10));
  EXPECT_EQ(11, kString.rfind('2', 10));
  EXPECT_EQ(12, kString.rfind('3', 10));
  EXPECT_EQ(13, kString.rfind('4', 10));
  EXPECT_EQ(14, kString.rfind('5', 10));
  EXPECT_EQ(15, kString.rfind('6', 10));
  EXPECT_EQ(16, kString.rfind('7', 10));
  EXPECT_EQ(17, kString.rfind('8', 10));
  EXPECT_EQ(18, kString.rfind('9', 10));
  EXPECT_EQ(19, kString.rfind('0', 10));
}

TEST(StringViewTest, RrfindReturnsNposWhenNoCharTypeMatch) {
  constexpr fit::string_view kString = "123456789123456789";

  EXPECT_EQ(fit::string_view::npos, kString.rfind('0'));
}

TEST(StringViewTest, RrfindReturnsFirstMatch) {
  constexpr fit::string_view kString = "12345678901234567890";

  EXPECT_EQ(19, kString.rfind(""));
  EXPECT_EQ(10, kString.rfind("12"));
  EXPECT_EQ(11, kString.rfind("23"));
  EXPECT_EQ(12, kString.rfind("34"));
  EXPECT_EQ(13, kString.rfind("45"));
  EXPECT_EQ(14, kString.rfind("56"));
  EXPECT_EQ(15, kString.rfind("67"));
  EXPECT_EQ(16, kString.rfind("78"));
  EXPECT_EQ(17, kString.rfind("89"));
  EXPECT_EQ(18, kString.rfind("90"));
  EXPECT_EQ(9, kString.rfind("01"));

  EXPECT_EQ(9, kString.rfind("01234"));
}

TEST(StringViewTest, RrfindWithPosReturnsFirstMatch) {
  constexpr fit::string_view kString = "12345678901234567890";

  EXPECT_EQ(19, kString.rfind("", 10));
  EXPECT_EQ(10, kString.rfind("1", 10));
  EXPECT_EQ(11, kString.rfind("2", 10));
  EXPECT_EQ(12, kString.rfind("3", 10));
  EXPECT_EQ(13, kString.rfind("4", 10));
  EXPECT_EQ(14, kString.rfind("5", 10));
  EXPECT_EQ(15, kString.rfind("6", 10));
  EXPECT_EQ(16, kString.rfind("7", 10));
  EXPECT_EQ(17, kString.rfind("8", 10));
  EXPECT_EQ(18, kString.rfind("9", 10));
  EXPECT_EQ(19, kString.rfind("0", 10));

  // String of size > 1.
  EXPECT_EQ(13, kString.rfind("456", 10));
}

TEST(StringViewTest, RrfindReturnsNposWhenNoMatch) {
  constexpr fit::string_view kString = "12345678901234567890";

  EXPECT_EQ(fit::string_view::npos, kString.rfind("A"));
  EXPECT_EQ(fit::string_view::npos, kString.rfind("02"));
  EXPECT_EQ(fit::string_view::npos, kString.rfind("42321"));
  EXPECT_EQ(fit::string_view::npos, kString.rfind('A'));
}

TEST(StringViewTest, RfindReturnsNposWhenNeedleIsBiggerThanHaystack) {
  constexpr fit::string_view kString = "123";

  // String of size > 1.
  EXPECT_EQ(fit::string_view::npos, kString.rfind("1234"));
  EXPECT_EQ(fit::string_view::npos, fit::string_view().find('1'));
}

TEST(StringViewTest, FindFirstOfReturnsFirstMatch) {
  constexpr fit::string_view kString = "ABCDE1234ABCDE1234";
  constexpr fit::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(5, kString.find_first_of("321"));
  EXPECT_EQ(5, kString.find_first_of("123"));
  EXPECT_EQ(5, kString.find_first_of("231"));
  EXPECT_EQ(5, kString.find_first_of("213"));

  EXPECT_EQ(5, kString.find_first_of(kMatchers));
  EXPECT_EQ(6, kString.find_first_of('2'));
}

TEST(StringViewTest, FindFirstOfWithPosReturnsFirstMatch) {
  constexpr fit::string_view kString = "ABCDE1234ABCDE1234";
  constexpr fit::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(14, kString.find_first_of("321", 9));
  EXPECT_EQ(14, kString.find_first_of("123", 9));
  EXPECT_EQ(14, kString.find_first_of("231", 9));
  EXPECT_EQ(14, kString.find_first_of("213", 9));

  EXPECT_EQ(14, kString.find_first_of(kMatchers, 9));
  EXPECT_EQ(5, kString.find_first_of('1'));
}

TEST(StringViewTest, FindFirstOfWithPosAndCountReturnsFirstMatch) {
  constexpr fit::string_view kString = "ABCDE1234ABCDE1234";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(14, kString.find_first_of("123", 9, 1));
  EXPECT_EQ(15, kString.find_first_of("231", 9, 1));
  EXPECT_EQ(15, kString.find_first_of("213", 9, 1));
  EXPECT_EQ(16, kString.find_first_of("321", 9, 1));
}

TEST(StringViewTest, FindFirstOfReturnsNposWhenNoMatch) {
  constexpr fit::string_view kString = "ABCDE1234ABCDE1234";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(fit::string_view::npos, kString.find_first_of("GHIJK"));
  EXPECT_EQ(fit::string_view::npos, kString.find_first_of("G"));
  EXPECT_EQ(fit::string_view::npos, kString.find_first_of('G'));
}

TEST(StringViewTest, FindLastOfReturnsLastMatch) {
  constexpr fit::string_view kString = "ABCDE1234ABCDE1234";
  constexpr fit::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change last match.
  EXPECT_EQ(16, kString.find_last_of("321"));
  EXPECT_EQ(16, kString.find_last_of("123"));
  EXPECT_EQ(16, kString.find_last_of("231"));
  EXPECT_EQ(16, kString.find_last_of("213"));

  EXPECT_EQ(16, kString.find_last_of(kMatchers));
  EXPECT_EQ(15, kString.find_last_of('2'));
}

TEST(StringViewTest, FindLastOfWithPosReturnsLastMatch) {
  constexpr fit::string_view kString = "ABCDE1234ABCDE1234";
  constexpr fit::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change last match.
  EXPECT_EQ(7, kString.find_last_of("321", 9));
  EXPECT_EQ(7, kString.find_last_of("123", 9));
  EXPECT_EQ(7, kString.find_last_of("231", 9));
  EXPECT_EQ(7, kString.find_last_of("213", 9));

  EXPECT_EQ(7, kString.find_last_of(kMatchers, 9));
  EXPECT_EQ(5, kString.find_last_of('1', 9));
}

TEST(StringViewTest, FindLastOfWithPosAndCountReturnsLastMatch) {
  constexpr fit::string_view kString = "ABCDE1234ABCDE1234";

  // Verify that order of chartacters in |s| does not change last match.
  EXPECT_EQ(5, kString.find_last_of("123", 9, 1));
  EXPECT_EQ(6, kString.find_last_of("231", 9, 1));
  EXPECT_EQ(6, kString.find_last_of("213", 9, 1));
  EXPECT_EQ(7, kString.find_last_of("321", 9, 1));
}

TEST(StringViewTest, FindLastOfReturnsNposWhenNoMatch) {
  constexpr fit::string_view kString = "ABCDE1234ABCDE1234";

  // Verify that order of chartacters in |s| does not change last match.
  EXPECT_EQ(fit::string_view::npos, kString.find_last_of("GHIJK"));
  EXPECT_EQ(fit::string_view::npos, kString.find_last_of("G"));
  EXPECT_EQ(fit::string_view::npos, kString.find_last_of('G'));
}

TEST(StringViewTest, FindFirstNofOfReturnsFirstNonMatch) {
  constexpr fit::string_view kString = "123ABC123";
  constexpr fit::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(0, kString.find_first_not_of(""));
  EXPECT_EQ(3, kString.find_first_not_of("321"));
  EXPECT_EQ(3, kString.find_first_not_of("123"));
  EXPECT_EQ(3, kString.find_first_not_of("231"));
  EXPECT_EQ(3, kString.find_first_not_of("213"));

  EXPECT_EQ(3, kString.find_first_not_of(kMatchers));
  EXPECT_EQ(1, kString.find_first_not_of('1'));
}

TEST(StringViewTest, FindFirstNofOfWithPosReturnsFirstNonMatch) {
  constexpr fit::string_view kString = "123ABC123A";
  constexpr fit::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(6, kString.find_first_not_of("", 6));
  EXPECT_EQ(9, kString.find_first_not_of("321", 6));
  EXPECT_EQ(9, kString.find_first_not_of("123", 6));
  EXPECT_EQ(9, kString.find_first_not_of("231", 6));
  EXPECT_EQ(9, kString.find_first_not_of("213", 6));

  EXPECT_EQ(9, kString.find_first_not_of(kMatchers, 9));
  EXPECT_EQ(7, kString.find_first_not_of('1', 6));
}

TEST(StringViewTest, FindFirstNofOfWithPosAndCountReturnsFirstNofMatch) {
  constexpr fit::string_view kString = "123ABC123A";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(7, kString.find_first_not_of("123", 6, 1));
  EXPECT_EQ(6, kString.find_first_not_of("231", 6, 1));
  EXPECT_EQ(6, kString.find_first_not_of("213", 6, 1));
  EXPECT_EQ(6, kString.find_first_not_of("321", 6, 1));
}

TEST(StringViewTest, FindFirstNofOfReturnsNposWhenNoMatch) {
  constexpr fit::string_view kString = "GGGGGGGGGGGGG";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(fit::string_view::npos, kString.find_first_not_of("ABCG"));
  EXPECT_EQ(fit::string_view::npos, kString.find_first_not_of("G"));
  EXPECT_EQ(fit::string_view::npos, kString.find_first_not_of('G'));
}

TEST(StringViewTest, FindLastNotOfReturnsLastMatch) {
  constexpr fit::string_view kString = "ABCDE1234ABCDE1234";
  constexpr fit::string_view kMatchers = "1234";

  // Verify that order of chartacters in |s| does not change last_not match.
  EXPECT_EQ(13, kString.find_last_not_of("3214"));
  EXPECT_EQ(13, kString.find_last_not_of("1234"));
  EXPECT_EQ(13, kString.find_last_not_of("2314"));
  EXPECT_EQ(13, kString.find_last_not_of("2134"));

  EXPECT_EQ(13, kString.find_last_not_of(kMatchers));
  EXPECT_EQ(16, kString.find_last_not_of('4'));
}

TEST(StringViewTest, FindLastNotOfWithPosReturnsLastMatch) {
  constexpr fit::string_view kString = "ABCDE1234ABCDE1234";
  constexpr fit::string_view kMatchers = "1234";

  // Verify that order of chartacters in |s| does not change last_not match.
  EXPECT_EQ(4, kString.find_last_not_of("3214", 8));
  EXPECT_EQ(4, kString.find_last_not_of("1234", 8));
  EXPECT_EQ(4, kString.find_last_not_of("2314", 8));
  EXPECT_EQ(4, kString.find_last_not_of("2134", 8));

  EXPECT_EQ(4, kString.find_last_not_of(kMatchers, 8));
  EXPECT_EQ(7, kString.find_last_not_of('4', 8));
}

TEST(StringViewTest, FindLastNotOfWithPosAndCountReturnsLastMatch) {
  constexpr fit::string_view kString = "ABCDE1234ABCDE1234";

  // Verify that order of chartacters in |s| does not change last_not match.
  EXPECT_EQ(8, kString.find_last_not_of("1234", 8, 1));
  EXPECT_EQ(8, kString.find_last_not_of("2314", 8, 2));
  EXPECT_EQ(5, kString.find_last_not_of("4321", 8, 3));
  EXPECT_EQ(4, kString.find_last_not_of("3214", 8, 4));
}

TEST(StringViewTest, FindLastNotOfReturnsNposWhenNoMatch) {
  constexpr fit::string_view kString = "GGGGGGG";

  // Verify that order of chartacters in |s| does not change last_not match.
  EXPECT_EQ(fit::string_view::npos, kString.find_last_not_of("GHIJK"));
  EXPECT_EQ(fit::string_view::npos, kString.find_last_not_of("G"));
  EXPECT_EQ(fit::string_view::npos, kString.find_last_not_of('G'));
}

TEST(StringViewTest, HashVaidation) {
  constexpr fit::string_view kStringView1 = "1234";
  constexpr fit::string_view kStringView2 = "123456";
  const std::string kString = "123";

  // Hash of the full view matches the hash of an equivalent string.
  EXPECT_EQ(std::hash<std::string>()(std::string(kStringView1.data())),
            std::hash<fit::string_view>()(kStringView1));
  EXPECT_EQ(std::hash<std::string>()(std::string(kStringView2.data())),
            std::hash<fit::string_view>()(kStringView2));

  // Hash uses the view, not the full string.
  EXPECT_EQ(std::hash<fit::string_view>()(kStringView1),
            std::hash<fit::string_view>()(kStringView2.substr(0, kStringView1.length())));

  // Hash matches the hash for a string with the same content.
  EXPECT_EQ(std::hash<std::string>()(kString),
            std::hash<fit::string_view>()(kStringView2.substr(0, kString.length())));
  EXPECT_EQ(std::hash<std::string>()(kString),
            std::hash<fit::string_view>()(kStringView1.substr(0, kString.length())));

  // If the hash of the contents are different in the default hash of the string, they should be
  // different in the specialized hash. We should make no assumptions on whether the values will hit
  // the same bucket or not.
  EXPECT_EQ(
      std::hash<std::string>()(std::string(kStringView1.data())) !=
          std::hash<std::string>()(std::string(kStringView2.data())),
      std::hash<fit::string_view>()(kStringView1) != std::hash<fit::string_view>()(kStringView2));
}

TEST(StringViewTest, OutputStreamOperatorFitsWithinWidth) {
  constexpr fit::string_view kStringView = "1234";
  std::ostringstream oss;
  oss.width(kStringView.length());

  oss << kStringView;

  EXPECT_EQ(oss.str(), kStringView);
}

TEST(StringViewTest, OutputStreamOperatorDoesNotFitInWidth) {
  constexpr fit::string_view kStringView = "1234";
  std::ostringstream oss;
  oss.width(kStringView.length() - 1);

  oss << kStringView;

  EXPECT_EQ(oss.str(), kStringView.substr(0, kStringView.length() - 1));
}

TEST(StringViewTest, OutputStreamOperatorFillsExtraSpaceToTheRight) {
  constexpr fit::string_view kStringView = "1234";
  constexpr fit::string_view kExpected = "000001234";
  std::ostringstream oss;
  oss.fill('0');
  oss.width(kExpected.length());

  oss << kStringView;

  EXPECT_EQ(oss.str(), kExpected);
}

TEST(StringViewTest, OutputStreamOperatorFillsExtraSpaceToTheLeft) {
  constexpr fit::string_view kStringView = "1234";
  constexpr fit::string_view kExpected = "123400000";
  std::ostringstream oss;
  oss.fill('0');
  oss.setf(std::ios_base::left);
  oss.width(kExpected.length());

  oss << kStringView;

  EXPECT_EQ(oss.str(), kExpected);
}

TEST(StringViewTest, OutputStreamOperatorResetsWidthToZero) {
  constexpr fit::string_view kStringView = "1234";
  std::ostringstream oss;
  oss.fill('0');
  oss.width(10);

  oss << kStringView;

  EXPECT_EQ(0, oss.width());
}

}  // namespace
}  // namespace fit
