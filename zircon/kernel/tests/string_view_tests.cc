// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <string.h>

#include <ktl/limits.h>
#include <ktl/string_view.h>

namespace {

bool CreateFromCArray() {
  BEGIN_TEST;
  constexpr char kStr[] = "1";
  ktl::string_view v_str(kStr);

  EXPECT_FALSE(v_str.empty());
  EXPECT_EQ(kStr, v_str.data());
  EXPECT_EQ(strlen(kStr), v_str.length());

  END_TEST;
}

bool CreateFromConstChar() {
  BEGIN_TEST;
  const char* kStr = "1";
  ktl::string_view v_str(kStr);

  EXPECT_FALSE(v_str.empty());
  EXPECT_EQ(kStr, v_str.data());
  EXPECT_EQ(strlen(kStr), v_str.length());

  END_TEST;
}

bool CreateFromStringView() {
  BEGIN_TEST;
  ktl::string_view str_view("12345");
  ktl::string_view v_str(str_view);

  EXPECT_FALSE(v_str.empty());
  EXPECT_EQ(str_view.data(), v_str.data());
  EXPECT_EQ(str_view.length(), v_str.length());

  END_TEST;
}

bool CreateFromConstexprStringView() {
  BEGIN_TEST;
  constexpr ktl::string_view kLiteral = "12345";
  ktl::string_view v_str(kLiteral);

  EXPECT_EQ(kLiteral.data(), v_str.data());
  EXPECT_EQ(kLiteral.length(), v_str.length());

  END_TEST;
}

bool CreateFromConstexprStringViewConstructor() {
  BEGIN_TEST;
  constexpr ktl::string_view kLiteral("12345");

  EXPECT_EQ(size_t{5}, kLiteral.size());
  EXPECT_EQ(size_t{5}, kLiteral.length());

  END_TEST;
}

bool CreateFromStringViewLiteral() {
  BEGIN_TEST;
  constexpr ktl::string_view kLiteral = "12345"sv;

  EXPECT_EQ(size_t{5}, kLiteral.size());
  EXPECT_EQ(size_t{5}, kLiteral.length());

  END_TEST;
}

bool SizeIsSameAsLength() {
  BEGIN_TEST;
  constexpr ktl::string_view kLiteral = "12345";

  EXPECT_EQ(size_t{5}, kLiteral.size());
  EXPECT_EQ(size_t{5}, kLiteral.length());

  END_TEST;
}

bool ArrayAccessOperator() {
  BEGIN_TEST;
  // Need static duration to enforce subobject constexpr, otherwise it is not allowed.
  constexpr static char const kLiteral[] = "12345";
  constexpr ktl::string_view kSvLiteral(kLiteral);

  for (size_t i = 0; i < kSvLiteral.size(); ++i) {
    EXPECT_EQ(kLiteral[i], kSvLiteral[i], "Array access returned wrong value.");
    EXPECT_EQ(&kLiteral[i], &kSvLiteral[i], "Array access returned value at different address.");
  }

  END_TEST;
}

bool BeginPointsToFirstElement() {
  BEGIN_TEST;
  constexpr ktl::string_view kLiteral = "12345";

  EXPECT_EQ(&kLiteral[0], &(*kLiteral.begin()));
  EXPECT_EQ(&kLiteral[4], &(*kLiteral.rbegin()));

  END_TEST;
}

bool EndPointsOnePastLastElement() {
  BEGIN_TEST;
  constexpr ktl::string_view kLiteral = "12345";

  EXPECT_EQ(&kLiteral[4], &(*(kLiteral.end() - 1)));
  EXPECT_EQ(&kLiteral[0], &(*(kLiteral.rend() - 1)));

  END_TEST;
}

bool EndPointsPastLastElement() {
  BEGIN_TEST;
  constexpr ktl::string_view kLiteral = "12345";

  EXPECT_EQ(kLiteral.begin() + 5, kLiteral.end());
  EXPECT_TRUE(kLiteral.rbegin() + 5 == kLiteral.rend());

  END_TEST;
}

bool WhenEmptyBeginIsSameAsEnd() {
  BEGIN_TEST;
  constexpr ktl::string_view kLiteral = "";

  EXPECT_EQ(kLiteral.begin(), kLiteral.end());
  EXPECT_TRUE(kLiteral.rbegin() == kLiteral.rend());

  END_TEST;
}

bool FrontReturnsRefToFirstElement() {
  BEGIN_TEST;
  constexpr ktl::string_view kLiteral = "12345";

  EXPECT_EQ(&(*kLiteral.begin()), &kLiteral.front());

  END_TEST;
}

bool BackReturnsRefToLastElement() {
  BEGIN_TEST;
  constexpr ktl::string_view kLiteral = "12345";

  EXPECT_EQ(&(*(kLiteral.begin() + 4)), &kLiteral.back());

  END_TEST;
}

bool EmptyIsTrueForEmptyString() {
  BEGIN_TEST;
  constexpr ktl::string_view kStr;

  ASSERT_TRUE(kStr.empty());
  ASSERT_EQ(size_t{0}, kStr.size());
  ASSERT_EQ(size_t{0}, kStr.length());

  END_TEST;
}

bool AtReturnsElementAtIndex() {
  BEGIN_TEST;
  // Need static duration to enforce subobject constexpr, otherwise it is not allowed.
  constexpr static char const kLiteral[] = "12345";
  constexpr ktl::string_view kSvLiteral(kLiteral);

  for (size_t i = 0; i < kSvLiteral.size(); ++i) {
    EXPECT_EQ(kLiteral[i], kSvLiteral.at(i), "Array access returned wrong value.");
    EXPECT_EQ(&kLiteral[i], &kSvLiteral.at(i), "Array access returned value at different address.");
  }

  END_TEST;
}

#if DEATH_TESTS
bool AtThrowsExceptionWhenIndexIsOOR() {
  BEGIN_TEST;
  ASSERT_DEATH([] {
    constexpr ktl::string_view kSvLiteral("12345");
    kSvLiteral.at(5);
  });

  END_TEST;
}
#endif

// Even though we use a custom compare implementation, because we lack a constexpr compare
// function, we use this test to verify that the expectations are equivalent.
bool CompareVerification() {
  BEGIN_TEST;
  constexpr ktl::string_view kStr1 = "1234";

  // Same string
  {
    constexpr ktl::string_view kStr2 = "1234";
    constexpr ktl::string_view kStr3 = "01234";
    EXPECT_EQ(0, ktl::string_view::traits_type::compare(kStr1.data(), kStr2.data(), 4));

    EXPECT_EQ(0, kStr1.compare(kStr2));
    EXPECT_EQ(0, kStr3.compare(1, kStr3.length() - 1, kStr2));
    EXPECT_EQ(0, kStr1.compare(1, kStr1.length() - 2, kStr2, 1, kStr2.length() - 2));

    EXPECT_EQ(0, kStr1.compare("1234"));
    EXPECT_EQ(0, kStr1.compare(1, kStr1.length() - 1, "234"));
    EXPECT_EQ(0, kStr1.compare(2, kStr1.length() - 2, "234", 1, 2));
  }

  // Same Length higher character
  {
    constexpr ktl::string_view kStr2 = "1235";
    EXPECT_LT(ktl::string_view::traits_type::compare(kStr1.data(), kStr2.data(), 4), 0);

    EXPECT_LT(kStr1.compare(kStr2), 0);
    EXPECT_LT(kStr1.compare(0, kStr1.length(), kStr2), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 2, kStr2, 1, kStr2.length() - 1), 0);

    EXPECT_LT(kStr1.compare("1235"), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 1, "235"), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 2, "1235", 1, 3), 0);
  }

  // Same Length lower character
  {
    constexpr ktl::string_view kStr2 = "1232";
    EXPECT_GT(ktl::string_view::traits_type::compare(kStr1.data(), kStr2.data(), 4), 0);

    EXPECT_GT(kStr1.compare(kStr2), 0);
    EXPECT_GT(kStr2.compare(1, kStr2.length() - 1, kStr1), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 1, kStr2, 1, kStr2.length() - 1), 0);

    EXPECT_GT(kStr1.compare("1232"), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 1, "232"), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 2, "22", 1, kStr2.length() - 2), 0);
  }

  // Greater Length
  {
    constexpr ktl::string_view kStr2 = "12345";
    constexpr ktl::string_view kStr3 = "2345";

    EXPECT_LT(kStr1.compare(kStr2), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 1, kStr3), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 1, kStr2, 1, kStr2.length() - 1), 0);

    EXPECT_LT(kStr1.compare(kStr2.data()), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 1, kStr3.data()), 0);
    EXPECT_LT(kStr1.compare(1, kStr1.length() - 1, kStr2.data(), 1, kStr2.length() - 1), 0);
  }

  // Shorter Length
  {
    constexpr ktl::string_view kStr2 = "123";
    constexpr ktl::string_view kStr3 = "23";

    EXPECT_GT(kStr1.compare(kStr2), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 1, kStr3), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 1, kStr2, 1, kStr2.length() - 1), 0);

    EXPECT_GT(kStr1.compare(kStr2.data()), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 1, kStr3.data()), 0);
    EXPECT_GT(kStr1.compare(1, kStr1.length() - 1, kStr2.data(), 1, kStr2.length() - 1), 0);
  }

  END_TEST;
}

// Check that they calls are equivalent to what the standard expects.
bool CompareOverloadCheck() {
  BEGIN_TEST;
  constexpr ktl::string_view kString1 = "123";
  constexpr ktl::string_view kString2 = "1234";

  {
    ktl::string_view expected = kString1.substr(1, 2);
    EXPECT_EQ(kString1.substr(1, 2).compare(expected), kString1.compare(1, 2, expected));
  }

  {
    EXPECT_EQ(kString1.substr(1, 2).compare(kString2.substr(1, 2)),
              kString1.compare(1, 2, kString2, 1, 2));
  }

  { EXPECT_EQ(kString1.compare(ktl::string_view("123")), kString1.compare("123")); }

  {
    EXPECT_EQ(kString1.substr(1, 2).compare(ktl::string_view("123")),
              kString1.compare(1, 2, "123"));
  }

  {
    EXPECT_EQ(kString1.substr(1, 2).compare(ktl::string_view("123")),
              kString1.compare(1, 2, "123"));
  }

  END_TEST;
}

bool OperatorEq() {
  BEGIN_TEST;
  constexpr ktl::string_view kStrView = "Self1234";

  EXPECT_TRUE(kStrView == kStrView);
  EXPECT_TRUE(kStrView == ktl::string_view("Self1234"));
  EXPECT_TRUE(kStrView == ktl::string_view("Self12345").substr(0, kStrView.length()));
  EXPECT_TRUE(kStrView == "Self1234");
  EXPECT_TRUE("Self1234" == kStrView);

  END_TEST;
}

bool OperatorNe() {
  BEGIN_TEST;
  constexpr ktl::string_view kStrView = "Self1234";

  EXPECT_TRUE(kStrView != ktl::string_view());
  EXPECT_TRUE(kStrView != ktl::string_view("Self12345"));
  EXPECT_TRUE(kStrView != "Self12345");
  EXPECT_TRUE("Self12345" != kStrView);

  END_TEST;
}

bool OperatorLess() {
  BEGIN_TEST;
  constexpr ktl::string_view kStrView = "Self1234";

  EXPECT_TRUE(kStrView < "Self12345");
  EXPECT_TRUE("Self123" < kStrView);
  EXPECT_TRUE(kStrView < ktl::string_view("Self12345"));

  END_TEST;
}

bool OperatorLessOrEq() {
  BEGIN_TEST;
  constexpr ktl::string_view kStrView = "Self1234";

  EXPECT_TRUE(kStrView <= "Self12345");
  EXPECT_TRUE("Self123" <= kStrView);
  EXPECT_TRUE(kStrView <= ktl::string_view("Self12345"));
  EXPECT_TRUE(kStrView <= ktl::string_view("Self1234"));

  END_TEST;
}

bool OperatorGreater() {
  BEGIN_TEST;
  constexpr ktl::string_view kStrView = "Self1234";

  EXPECT_TRUE(kStrView > "Self123");
  EXPECT_TRUE("Self12345" > kStrView);
  EXPECT_TRUE(kStrView > ktl::string_view("Self123"));

  END_TEST;
}

bool OperatorGreaterOrEq() {
  BEGIN_TEST;
  constexpr ktl::string_view kStrView = "Self1234";

  EXPECT_TRUE(kStrView >= "Self123");
  EXPECT_TRUE("Self12345" >= kStrView);
  EXPECT_TRUE(kStrView >= ktl::string_view("Self123"));
  EXPECT_TRUE(kStrView >= ktl::string_view("Self1234"));

  END_TEST;
}

bool RemovePrefix() {
  BEGIN_TEST;
  constexpr ktl::string_view kPrefixWithSuffix = "PrefixSuffix";
  ktl::string_view str_view = kPrefixWithSuffix;

  str_view.remove_prefix(6);
  EXPECT_EQ(kPrefixWithSuffix.length() - 6, str_view.length());
  auto no_suffix = kPrefixWithSuffix.substr(6, kPrefixWithSuffix.length() - 6);
  EXPECT_TRUE(no_suffix == str_view);
  EXPECT_TRUE("Suffix" == str_view);

  END_TEST;
}

bool RemoveSuffix() {
  BEGIN_TEST;
  constexpr ktl::string_view kPrefixWithSuffix = "PrefixSuffix";
  ktl::string_view str_view = kPrefixWithSuffix;

  str_view.remove_suffix(6);
  EXPECT_EQ(kPrefixWithSuffix.length() - 6, str_view.length());
  auto no_suffix = kPrefixWithSuffix.substr(0, kPrefixWithSuffix.length() - 6);
  EXPECT_TRUE(no_suffix == str_view);
  EXPECT_TRUE("Prefix" == str_view);

  END_TEST;
}

bool SubstrNoArgsAreEqual() {
  BEGIN_TEST;
  constexpr ktl::string_view kLiteral = "12345";

  EXPECT_TRUE(kLiteral == kLiteral.substr());

  END_TEST;
}

bool SubstrWithPosIsMatchesSubstring() {
  BEGIN_TEST;
  constexpr ktl::string_view kLiteral = "12345";
  constexpr ktl::string_view kExpectedLiteral = "345";

  EXPECT_TRUE(kExpectedLiteral == kLiteral.substr(2));

  END_TEST;
}

bool SubstrWithPosAndCountIsMatchesSubstring() {
  BEGIN_TEST;
  constexpr ktl::string_view kLiteral = "12345";
  constexpr ktl::string_view kExpectedLiteral = "34";

  EXPECT_TRUE(kExpectedLiteral == kLiteral.substr(2, 2));

  END_TEST;
}

bool Swap() {
  BEGIN_TEST;
  ktl::string_view str_1 = "12345";
  ktl::string_view str_2 = "34";

  str_1.swap(str_2);

  EXPECT_TRUE("34" == str_1);
  EXPECT_TRUE("12345" == str_2);

  END_TEST;
}

bool Copy() {
  BEGIN_TEST;
  constexpr ktl::string_view kBase = "Base";
  constexpr ktl::string_view::size_type kSize = 2;
  ktl::string_view::value_type dest[kSize + 1] = {'\0'};
  ktl::string_view::value_type dest_traits[kSize + 1] = {'\0'};

  EXPECT_EQ(kSize, kBase.copy(dest, kSize, 0));
  EXPECT_EQ(dest_traits, ktl::string_view::traits_type::copy(dest_traits, kBase.data(), kSize));
  EXPECT_EQ(0, strcmp(dest_traits, dest));

  END_TEST;
}

#if DEATH_TESTS
bool CopyThrowsExceptionOnOOR() {
  BEGIN_TEST;
  ASSERT_DEATH([]() {
    constexpr ktl::string_view v_str = "Base";
    ktl::string_view::value_type dest[v_str.length() + 2] = {};
    memset(dest, '\0', v_str.length() + 1);

    v_str.copy(dest, v_str.length(), v_str.length());
  });

  END_TEST;
}
#endif

bool MaxSizeIsMaxAddressableSize() {
  BEGIN_TEST;
  ktl::string_view str_view("12345");

  EXPECT_EQ(ktl::numeric_limits<ktl::string_view::size_type>::max(), str_view.max_size());

  END_TEST;
}

bool FindReturnsFirstCharTypeMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "12345678901234567890";

  EXPECT_EQ(size_t{0}, kString.find('1'));
  EXPECT_EQ(size_t{1}, kString.find('2'));
  EXPECT_EQ(size_t{2}, kString.find('3'));
  EXPECT_EQ(size_t{3}, kString.find('4'));
  EXPECT_EQ(size_t{4}, kString.find('5'));
  EXPECT_EQ(size_t{5}, kString.find('6'));
  EXPECT_EQ(size_t{6}, kString.find('7'));
  EXPECT_EQ(size_t{7}, kString.find('8'));
  EXPECT_EQ(size_t{8}, kString.find('9'));
  EXPECT_EQ(size_t{9}, kString.find('0'));

  END_TEST;
}

bool FindWithPosReturnsFirstCharTypeMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "12345678901234567890";

  EXPECT_EQ(size_t{10}, kString.find('1', 10));
  EXPECT_EQ(size_t{11}, kString.find('2', 10));
  EXPECT_EQ(size_t{12}, kString.find('3', 10));
  EXPECT_EQ(size_t{13}, kString.find('4', 10));
  EXPECT_EQ(size_t{14}, kString.find('5', 10));
  EXPECT_EQ(size_t{15}, kString.find('6', 10));
  EXPECT_EQ(size_t{16}, kString.find('7', 10));
  EXPECT_EQ(size_t{17}, kString.find('8', 10));
  EXPECT_EQ(size_t{18}, kString.find('9', 10));
  EXPECT_EQ(size_t{19}, kString.find('0', 10));

  END_TEST;
}

bool FindReturnsNposWhenNoCharTypeMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "123456789123456789";

  EXPECT_EQ(ktl::string_view::npos, kString.find('0'));

  END_TEST;
}

bool FindReturnsFirstMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "12345678901234567890";

  EXPECT_EQ(size_t{0}, kString.find(""));
  EXPECT_EQ(size_t{0}, kString.find("12"));
  EXPECT_EQ(size_t{1}, kString.find("23"));
  EXPECT_EQ(size_t{2}, kString.find("34"));
  EXPECT_EQ(size_t{3}, kString.find("45"));
  EXPECT_EQ(size_t{4}, kString.find("56"));
  EXPECT_EQ(size_t{5}, kString.find("67"));
  EXPECT_EQ(size_t{6}, kString.find("78"));
  EXPECT_EQ(size_t{7}, kString.find("89"));
  EXPECT_EQ(size_t{8}, kString.find("90"));
  EXPECT_EQ(size_t{9}, kString.find("01"));

  EXPECT_EQ(size_t{9}, kString.find("01234"));

  END_TEST;
}

bool FindWithPosReturnsFirstMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "12345678901234567890";

  EXPECT_EQ(size_t{10}, kString.find("", 10));
  EXPECT_EQ(size_t{10}, kString.find("1", 10));
  EXPECT_EQ(size_t{11}, kString.find("2", 10));
  EXPECT_EQ(size_t{12}, kString.find("3", 10));
  EXPECT_EQ(size_t{13}, kString.find("4", 10));
  EXPECT_EQ(size_t{14}, kString.find("5", 10));
  EXPECT_EQ(size_t{15}, kString.find("6", 10));
  EXPECT_EQ(size_t{16}, kString.find("7", 10));
  EXPECT_EQ(size_t{17}, kString.find("8", 10));
  EXPECT_EQ(size_t{18}, kString.find("9", 10));
  EXPECT_EQ(size_t{19}, kString.find("0", 10));

  // String of size > 1.
  EXPECT_EQ(size_t{13}, kString.find("456", 10));

  END_TEST;
}

bool FindReturnsNposWhenNoMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "12345678901234567890";

  // String of size > 1.
  EXPECT_EQ(ktl::string_view::npos, kString.find("A"));
  EXPECT_EQ(ktl::string_view::npos, kString.find("02"));
  EXPECT_EQ(ktl::string_view::npos, kString.find("42321"));

  END_TEST;
}

bool FindReturnsNposWhenNeedleIsBiggerThanHaystack() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "123";

  // String of size > 1.
  EXPECT_EQ(ktl::string_view::npos, kString.find("1234"));

  END_TEST;
}

bool RrfindReturnsFirstCharTypeMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "12345678901234567890";

  EXPECT_EQ(size_t{10}, kString.rfind('1'));
  EXPECT_EQ(size_t{11}, kString.rfind('2'));
  EXPECT_EQ(size_t{12}, kString.rfind('3'));
  EXPECT_EQ(size_t{13}, kString.rfind('4'));
  EXPECT_EQ(size_t{14}, kString.rfind('5'));
  EXPECT_EQ(size_t{15}, kString.rfind('6'));
  EXPECT_EQ(size_t{16}, kString.rfind('7'));
  EXPECT_EQ(size_t{17}, kString.rfind('8'));
  EXPECT_EQ(size_t{18}, kString.rfind('9'));
  EXPECT_EQ(size_t{19}, kString.rfind('0'));

  END_TEST;
}

bool RrfindWithPosReturnsFirstCharTypeMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "12345678901234567890";

  EXPECT_EQ(size_t{10}, kString.rfind('1', 10));
  EXPECT_EQ(size_t{11}, kString.rfind('2', 11));
  EXPECT_EQ(size_t{12}, kString.rfind('3', 12));
  EXPECT_EQ(size_t{13}, kString.rfind('4', 13));
  EXPECT_EQ(size_t{14}, kString.rfind('5', 14));
  EXPECT_EQ(size_t{15}, kString.rfind('6', 15));
  EXPECT_EQ(size_t{16}, kString.rfind('7', 16));
  EXPECT_EQ(size_t{17}, kString.rfind('8', 17));
  EXPECT_EQ(size_t{18}, kString.rfind('9', 18));
  EXPECT_EQ(size_t{19}, kString.rfind('0', 19));

  END_TEST;
}

bool RrfindReturnsNposWhenNoCharTypeMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "123456789123456789";

  EXPECT_EQ(ktl::string_view::npos, kString.rfind('0'));

  END_TEST;
}

bool RrfindReturnsFirstMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "12345678901234567890";

  EXPECT_EQ(size_t{10}, kString.rfind("12"));
  EXPECT_EQ(size_t{11}, kString.rfind("23"));
  EXPECT_EQ(size_t{12}, kString.rfind("34"));
  EXPECT_EQ(size_t{13}, kString.rfind("45"));
  EXPECT_EQ(size_t{14}, kString.rfind("56"));
  EXPECT_EQ(size_t{15}, kString.rfind("67"));
  EXPECT_EQ(size_t{16}, kString.rfind("78"));
  EXPECT_EQ(size_t{17}, kString.rfind("89"));
  EXPECT_EQ(size_t{18}, kString.rfind("90"));
  EXPECT_EQ(size_t{9}, kString.rfind("01"));

  EXPECT_EQ(size_t{9}, kString.rfind("01234"));

  END_TEST;
}

bool RrfindWithPosReturnsFirstMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "12345678901234567890";

  EXPECT_EQ(size_t{10}, kString.rfind("1", 10));
  EXPECT_EQ(size_t{11}, kString.rfind("2", 11));
  EXPECT_EQ(size_t{12}, kString.rfind("3", 12));
  EXPECT_EQ(size_t{13}, kString.rfind("4", 13));
  EXPECT_EQ(size_t{14}, kString.rfind("5", 14));
  EXPECT_EQ(size_t{15}, kString.rfind("6", 15));
  EXPECT_EQ(size_t{16}, kString.rfind("7", 16));
  EXPECT_EQ(size_t{17}, kString.rfind("8", 17));
  EXPECT_EQ(size_t{18}, kString.rfind("9", 18));
  EXPECT_EQ(size_t{19}, kString.rfind("0", 19));

  // String of size > 1.
  EXPECT_EQ(size_t{13}, kString.rfind("456", 13));

  END_TEST;
}

bool RrfindReturnsNposWhenNoMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "12345678901234567890";

  EXPECT_EQ(ktl::string_view::npos, kString.rfind("A"));
  EXPECT_EQ(ktl::string_view::npos, kString.rfind("02"));
  EXPECT_EQ(ktl::string_view::npos, kString.rfind("42321"));
  EXPECT_EQ(ktl::string_view::npos, kString.rfind('A'));

  END_TEST;
}

bool RfindReturnsNposWhenNeedleIsBiggerThanHaystack() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "123";

  // String of size > 1.
  EXPECT_EQ(ktl::string_view::npos, kString.rfind("1234"));
  EXPECT_EQ(ktl::string_view::npos, ktl::string_view().find('1'));

  END_TEST;
}

bool FindFirstOfReturnsFirstMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "ABCDE1234ABCDE1234";
  constexpr ktl::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(size_t{5}, kString.find_first_of("321"));
  EXPECT_EQ(size_t{5}, kString.find_first_of("123"));
  EXPECT_EQ(size_t{5}, kString.find_first_of("231"));
  EXPECT_EQ(size_t{5}, kString.find_first_of("213"));

  EXPECT_EQ(size_t{5}, kString.find_first_of(kMatchers));
  EXPECT_EQ(size_t{6}, kString.find_first_of('2'));

  END_TEST;
}

bool FindFirstOfWithPosReturnsFirstMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "ABCDE1234ABCDE1234";
  constexpr ktl::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(size_t{14}, kString.find_first_of("321", 9));
  EXPECT_EQ(size_t{14}, kString.find_first_of("123", 9));
  EXPECT_EQ(size_t{14}, kString.find_first_of("231", 9));
  EXPECT_EQ(size_t{14}, kString.find_first_of("213", 9));

  EXPECT_EQ(size_t{14}, kString.find_first_of(kMatchers, 9));
  EXPECT_EQ(size_t{5}, kString.find_first_of('1'));

  END_TEST;
}

bool FindFirstOfWithPosAndCountReturnsFirstMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "ABCDE1234ABCDE1234";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(size_t{14}, kString.find_first_of("123", 9, 1));
  EXPECT_EQ(size_t{15}, kString.find_first_of("231", 9, 1));
  EXPECT_EQ(size_t{15}, kString.find_first_of("213", 9, 1));
  EXPECT_EQ(size_t{16}, kString.find_first_of("321", 9, 1));

  END_TEST;
}

bool FindFirstOfReturnsNposWhenNoMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "ABCDE1234ABCDE1234";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(ktl::string_view::npos, kString.find_first_of("GHIJK"));
  EXPECT_EQ(ktl::string_view::npos, kString.find_first_of("G"));
  EXPECT_EQ(ktl::string_view::npos, kString.find_first_of('G'));

  END_TEST;
}

bool FindLastOfReturnsLastMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "ABCDE1234ABCDE1234";
  constexpr ktl::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change last match.
  EXPECT_EQ(size_t{16}, kString.find_last_of("321"));
  EXPECT_EQ(size_t{16}, kString.find_last_of("123"));
  EXPECT_EQ(size_t{16}, kString.find_last_of("231"));
  EXPECT_EQ(size_t{16}, kString.find_last_of("213"));

  EXPECT_EQ(size_t{16}, kString.find_last_of(kMatchers));
  EXPECT_EQ(size_t{15}, kString.find_last_of('2'));

  END_TEST;
}

bool FindLastOfWithPosReturnsLastMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "ABCDE1234ABCDE1234";
  constexpr ktl::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change last match.
  EXPECT_EQ(size_t{7}, kString.find_last_of("321", 9));
  EXPECT_EQ(size_t{7}, kString.find_last_of("123", 9));
  EXPECT_EQ(size_t{7}, kString.find_last_of("231", 9));
  EXPECT_EQ(size_t{7}, kString.find_last_of("213", 9));

  EXPECT_EQ(size_t{7}, kString.find_last_of(kMatchers, 9));
  EXPECT_EQ(size_t{5}, kString.find_last_of('1', 9));

  END_TEST;
}

bool FindLastOfWithPosAndCountReturnsLastMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "ABCDE1234ABCDE1234";

  // Verify that order of chartacters in |s| does not change last match.
  EXPECT_EQ(size_t{5}, kString.find_last_of("123", 9, 1));
  EXPECT_EQ(size_t{6}, kString.find_last_of("231", 9, 1));
  EXPECT_EQ(size_t{6}, kString.find_last_of("213", 9, 1));
  EXPECT_EQ(size_t{7}, kString.find_last_of("321", 9, 1));

  END_TEST;
}

bool FindLastOfReturnsNposWhenNoMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "ABCDE1234ABCDE1234";

  // Verify that order of chartacters in |s| does not change last match.
  EXPECT_EQ(ktl::string_view::npos, kString.find_last_of("GHIJK"));
  EXPECT_EQ(ktl::string_view::npos, kString.find_last_of("G"));
  EXPECT_EQ(ktl::string_view::npos, kString.find_last_of('G'));

  END_TEST;
}

bool FindFirstNofOfReturnsFirstNonMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "123ABC123";
  constexpr ktl::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(size_t{0}, kString.find_first_not_of(""));
  EXPECT_EQ(size_t{3}, kString.find_first_not_of("321"));
  EXPECT_EQ(size_t{3}, kString.find_first_not_of("123"));
  EXPECT_EQ(size_t{3}, kString.find_first_not_of("231"));
  EXPECT_EQ(size_t{3}, kString.find_first_not_of("213"));

  EXPECT_EQ(size_t{3}, kString.find_first_not_of(kMatchers));
  EXPECT_EQ(size_t{1}, kString.find_first_not_of('1'));

  END_TEST;
}

bool FindFirstNofOfWithPosReturnsFirstNonMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "123ABC123A";
  constexpr ktl::string_view kMatchers = "123";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(size_t{6}, kString.find_first_not_of("", 6));
  EXPECT_EQ(size_t{9}, kString.find_first_not_of("321", 6));
  EXPECT_EQ(size_t{9}, kString.find_first_not_of("123", 6));
  EXPECT_EQ(size_t{9}, kString.find_first_not_of("231", 6));
  EXPECT_EQ(size_t{9}, kString.find_first_not_of("213", 6));

  EXPECT_EQ(size_t{9}, kString.find_first_not_of(kMatchers, 9));
  EXPECT_EQ(size_t{7}, kString.find_first_not_of('1', 6));

  END_TEST;
}

bool FindFirstNofOfWithPosAndCountReturnsFirstNofMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "123ABC123A";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(size_t{7}, kString.find_first_not_of("123", 6, 1));
  EXPECT_EQ(size_t{6}, kString.find_first_not_of("231", 6, 1));
  EXPECT_EQ(size_t{6}, kString.find_first_not_of("213", 6, 1));
  EXPECT_EQ(size_t{6}, kString.find_first_not_of("321", 6, 1));

  END_TEST;
}

bool FindFirstNofOfReturnsNposWhenNoMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "GGGGGGGGGGGGG";

  // Verify that order of chartacters in |s| does not change first match.
  EXPECT_EQ(ktl::string_view::npos, kString.find_first_not_of("ABCG"));
  EXPECT_EQ(ktl::string_view::npos, kString.find_first_not_of("G"));
  EXPECT_EQ(ktl::string_view::npos, kString.find_first_not_of('G'));

  END_TEST;
}

bool FindLastNotOfReturnsLastMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "ABCDE1234ABCDE1234";
  constexpr ktl::string_view kMatchers = "1234";

  // Verify that order of chartacters in |s| does not change last_not match.
  EXPECT_EQ(size_t{13}, kString.find_last_not_of("3214"));
  EXPECT_EQ(size_t{13}, kString.find_last_not_of("1234"));
  EXPECT_EQ(size_t{13}, kString.find_last_not_of("2314"));
  EXPECT_EQ(size_t{13}, kString.find_last_not_of("2134"));

  EXPECT_EQ(size_t{13}, kString.find_last_not_of(kMatchers));
  EXPECT_EQ(size_t{16}, kString.find_last_not_of('4'));

  END_TEST;
}

bool FindLastNotOfWithPosReturnsLastMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "ABCDE1234ABCDE1234";
  constexpr ktl::string_view kMatchers = "1234";

  // Verify that order of chartacters in |s| does not change last_not match.
  EXPECT_EQ(size_t{4}, kString.find_last_not_of("3214", 8));
  EXPECT_EQ(size_t{4}, kString.find_last_not_of("1234", 8));
  EXPECT_EQ(size_t{4}, kString.find_last_not_of("2314", 8));
  EXPECT_EQ(size_t{4}, kString.find_last_not_of("2134", 8));

  EXPECT_EQ(size_t{4}, kString.find_last_not_of(kMatchers, 8));
  EXPECT_EQ(size_t{7}, kString.find_last_not_of('4', 8));

  END_TEST;
}

bool FindLastNotOfWithPosAndCountReturnsLastMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "ABCDE1234ABCDE1234";

  // Verify that order of chartacters in |s| does not change last_not match.
  EXPECT_EQ(size_t{8}, kString.find_last_not_of("1234", 8, 1));
  EXPECT_EQ(size_t{8}, kString.find_last_not_of("2314", 8, 2));
  EXPECT_EQ(size_t{5}, kString.find_last_not_of("4321", 8, 3));
  EXPECT_EQ(size_t{4}, kString.find_last_not_of("3214", 8, 4));

  END_TEST;
}

bool FindLastNotOfReturnsNposWhenNoMatch() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "GGGGGGG";

  // Verify that order of chartacters in |s| does not change last_not match.
  EXPECT_EQ(ktl::string_view::npos, kString.find_last_not_of("GHIJK"));
  EXPECT_EQ(ktl::string_view::npos, kString.find_last_not_of("G"));
  EXPECT_EQ(ktl::string_view::npos, kString.find_last_not_of('G'));

  END_TEST;
}

bool StartsWith() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "foobar";

  // string_view argument.
  EXPECT_TRUE(kString.starts_with("foo"sv));
  EXPECT_FALSE(kString.starts_with("bar"sv));

  // char argument.
  EXPECT_TRUE(kString.starts_with('f'));
  EXPECT_FALSE(kString.starts_with('b'));

  // C string (const char*) argument.
  EXPECT_TRUE(kString.starts_with("foo"));
  EXPECT_FALSE(kString.starts_with("bar"));

  END_TEST;
}

bool EndsWith() {
  BEGIN_TEST;
  constexpr ktl::string_view kString = "foobar";

  // string_view argument.
  EXPECT_TRUE(kString.ends_with("bar"sv));
  EXPECT_FALSE(kString.ends_with("foo"sv));

  // char argument.
  EXPECT_TRUE(kString.ends_with('r'));
  EXPECT_FALSE(kString.ends_with('f'));

  // C string (const char*) argument.
  EXPECT_TRUE(kString.ends_with("bar"));
  EXPECT_FALSE(kString.ends_with("foo"));

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(string_view_tests)
UNITTEST("CreateFromCArray", CreateFromCArray)
UNITTEST("CreateFromConstChar", CreateFromConstChar)
UNITTEST("CreateFromStringView", CreateFromStringView)
UNITTEST("CreateFromConstexprStringView", CreateFromConstexprStringView)
UNITTEST("CreateFromConstexprStringViewConstructor", CreateFromConstexprStringViewConstructor)
UNITTEST("CreateFromStringViewLiteral", CreateFromStringViewLiteral)
UNITTEST("SizeIsSameAsLength", SizeIsSameAsLength)
UNITTEST("ArrayAccessOperator", ArrayAccessOperator)
UNITTEST("BeginPointsToFirstElement", BeginPointsToFirstElement)
UNITTEST("EndPointsOnePastLastElement", EndPointsOnePastLastElement)
UNITTEST("EndPointsPastLastElement", EndPointsPastLastElement)
UNITTEST("WhenEmptyBeginIsSameAsEnd", WhenEmptyBeginIsSameAsEnd)
UNITTEST("FrontReturnsRefToFirstElement", FrontReturnsRefToFirstElement)
UNITTEST("BackReturnsRefToLastElement", BackReturnsRefToLastElement)
UNITTEST("EmptyIsTrueForEmptyString", EmptyIsTrueForEmptyString)
UNITTEST("AtReturnsElementAtIndex", AtReturnsElementAtIndex)
UNITTEST("CompareVerification", CompareVerification)
UNITTEST("CompareOverloadCheck", CompareOverloadCheck)
UNITTEST("OperatorEq", OperatorEq)
UNITTEST("OperatorNe", OperatorNe)
UNITTEST("OperatorLess", OperatorLess)
UNITTEST("OperatorLessOrEq", OperatorLessOrEq)
UNITTEST("OperatorGreater", OperatorGreater)
UNITTEST("OperatorGreaterOrEq", OperatorGreaterOrEq)
UNITTEST("RemovePrefix", RemovePrefix)
UNITTEST("RemoveSuffix", RemoveSuffix)
UNITTEST("SubstrNoArgsAreEqual", SubstrNoArgsAreEqual)
UNITTEST("SubstrWithPosIsMatchesSubstring", SubstrWithPosIsMatchesSubstring)
UNITTEST("SubstrWithPosAndCountIsMatchesSubstring", SubstrWithPosAndCountIsMatchesSubstring)
UNITTEST("Swap", Swap)
UNITTEST("Copy", Copy)
UNITTEST("MaxSizeIsMaxAddressableSize", MaxSizeIsMaxAddressableSize)
UNITTEST("FindReturnsFirstCharTypeMatch", FindReturnsFirstCharTypeMatch)
UNITTEST("FindWithPosReturnsFirstCharTypeMatch", FindWithPosReturnsFirstCharTypeMatch)
UNITTEST("FindReturnsNposWhenNoCharTypeMatch", FindReturnsNposWhenNoCharTypeMatch)
UNITTEST("FindReturnsFirstMatch", FindReturnsFirstMatch)
UNITTEST("FindWithPosReturnsFirstMatch", FindWithPosReturnsFirstMatch)
UNITTEST("FindReturnsNposWhenNoMatch", FindReturnsNposWhenNoMatch)
UNITTEST("FindReturnsNposWhenNeedleIsBiggerThanHaystack",
         FindReturnsNposWhenNeedleIsBiggerThanHaystack)
UNITTEST("RrfindReturnsFirstCharTypeMatch", RrfindReturnsFirstCharTypeMatch)
UNITTEST("RrfindWithPosReturnsFirstCharTypeMatch", RrfindWithPosReturnsFirstCharTypeMatch)
UNITTEST("RrfindReturnsNposWhenNoCharTypeMatch", RrfindReturnsNposWhenNoCharTypeMatch)
UNITTEST("RrfindReturnsFirstMatch", RrfindReturnsFirstMatch)
UNITTEST("RrfindWithPosReturnsFirstMatch", RrfindWithPosReturnsFirstMatch)
UNITTEST("RrfindReturnsNposWhenNoMatch", RrfindReturnsNposWhenNoMatch)
UNITTEST("RfindReturnsNposWhenNeedleIsBiggerThanHaystack",
         RfindReturnsNposWhenNeedleIsBiggerThanHaystack)
UNITTEST("FindFirstOfReturnsFirstMatch", FindFirstOfReturnsFirstMatch)
UNITTEST("FindFirstOfWithPosReturnsFirstMatch", FindFirstOfWithPosReturnsFirstMatch)
UNITTEST("FindFirstOfWithPosAndCountReturnsFirstMatch", FindFirstOfWithPosAndCountReturnsFirstMatch)
UNITTEST("FindFirstOfReturnsNposWhenNoMatch", FindFirstOfReturnsNposWhenNoMatch)
UNITTEST("FindLastOfReturnsLastMatch", FindLastOfReturnsLastMatch)
UNITTEST("FindLastOfWithPosReturnsLastMatch", FindLastOfWithPosReturnsLastMatch)
UNITTEST("FindLastOfWithPosAndCountReturnsLastMatch", FindLastOfWithPosAndCountReturnsLastMatch)
UNITTEST("FindLastOfReturnsNposWhenNoMatch", FindLastOfReturnsNposWhenNoMatch)
UNITTEST("FindFirstNofOfReturnsFirstNonMatch", FindFirstNofOfReturnsFirstNonMatch)
UNITTEST("FindFirstNofOfWithPosReturnsFirstNonMatch", FindFirstNofOfWithPosReturnsFirstNonMatch)
UNITTEST("FindFirstNofOfWithPosAndCountReturnsFirstNofMatch",
         FindFirstNofOfWithPosAndCountReturnsFirstNofMatch)
UNITTEST("FindFirstNofOfReturnsNposWhenNoMatch", FindFirstNofOfReturnsNposWhenNoMatch)
UNITTEST("FindLastNotOfReturnsLastMatch", FindLastNotOfReturnsLastMatch)
UNITTEST("FindLastNotOfWithPosReturnsLastMatch", FindLastNotOfWithPosReturnsLastMatch)
UNITTEST("FindLastNotOfWithPosAndCountReturnsLastMatch",
         FindLastNotOfWithPosAndCountReturnsLastMatch)
UNITTEST("FindLastNotOfReturnsNposWhenNoMatch", FindLastNotOfReturnsNposWhenNoMatch)
UNITTEST("StartsWith", StartsWith)
UNITTEST("EndsWith", EndsWith)
UNITTEST_END_TESTCASE(string_view_tests, "string_view", "ktl::string_view tests")
