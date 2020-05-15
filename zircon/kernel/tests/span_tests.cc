// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <ktl/byte.h>
#include <ktl/span.h>
#include <ktl/type_traits.h>

namespace {

static constexpr int kDigitsArray[] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
static constexpr ktl::span<const int> kDigits = kDigitsArray;  // T (const &)[N] constructor
static constexpr ktl::span<const int> kMiddleDigits = kDigits.subspan(3, 4);
static constexpr ktl::span<const int> kLastDigits = kDigits.subspan(7);
static constexpr ktl::span<const int> kEmpty = kDigits.subspan(0, 0);
static constexpr ktl::span<const int> kDefault;  // default constructor

bool FrontTest() {
  BEGIN_TEST;

  EXPECT_EQ(kDigits.front(), 9);
  EXPECT_EQ(kMiddleDigits.front(), 6);
  EXPECT_EQ(kLastDigits.front(), 2);

  END_TEST;
}

bool BackTest() {
  BEGIN_TEST;

  EXPECT_EQ(kDigits.back(), 0);
  EXPECT_EQ(kMiddleDigits.back(), 3);
  EXPECT_EQ(kLastDigits.back(), 0);

  END_TEST;
}

bool IndexTest() {
  BEGIN_TEST;

  EXPECT_EQ(kDigits[0], 9);
  EXPECT_EQ(kDigits[9], 0);
  EXPECT_EQ(kDigits[4], 5);

  EXPECT_EQ(kMiddleDigits[0], 6);
  EXPECT_EQ(kMiddleDigits[3], 3);
  EXPECT_EQ(kMiddleDigits[1], 5);

  EXPECT_EQ(kLastDigits[0], 2);
  EXPECT_EQ(kLastDigits[1], 1);
  EXPECT_EQ(kLastDigits[2], 0);

  END_TEST;
}

bool SizeEmptyTest() {
  BEGIN_TEST;

  EXPECT_EQ(kDigits.size(), 10u);
  EXPECT_EQ(kMiddleDigits.size(), 4u);
  EXPECT_EQ(kLastDigits.size(), 3u);
  EXPECT_EQ(kEmpty.size(), 0u);
  EXPECT_EQ(kDefault.size(), 0u);

  EXPECT_FALSE(kDigits.empty());
  EXPECT_FALSE(kMiddleDigits.empty());
  EXPECT_FALSE(kLastDigits.empty());
  EXPECT_TRUE(kEmpty.empty());
  EXPECT_TRUE(kDefault.empty());

  EXPECT_EQ(kDigits.size_bytes(), 10 * sizeof(int));
  EXPECT_EQ(kMiddleDigits.size_bytes(), 4 * sizeof(int));
  EXPECT_EQ(kLastDigits.size_bytes(), 3 * sizeof(int));
  EXPECT_EQ(kEmpty.size_bytes(), 0u);
  EXPECT_EQ(kDefault.size_bytes(), 0u);

  END_TEST;
}

bool DataReferencesTest() {
  BEGIN_TEST;

  EXPECT_EQ(kDigits.data(), kDigitsArray);
  EXPECT_EQ(kMiddleDigits.data(), kDigitsArray + 3);
  EXPECT_EQ(kLastDigits.data(), kDigitsArray + 7);

  EXPECT_EQ(&*kDigits.begin(), kDigitsArray);
  EXPECT_EQ(&*kDigits.end(), kDigitsArray + 10);
  EXPECT_EQ(&*kMiddleDigits.begin(), kDigitsArray + 3);

  EXPECT_EQ(&kDigits[0], kDigitsArray);
  EXPECT_EQ(&kDigits.back(), kDigitsArray + 9);
  EXPECT_EQ(&kMiddleDigits[2], kDigitsArray + 5);

  END_TEST;
}

bool IteratorsTest() {
  BEGIN_TEST;

  int digits_array[] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

  ktl::span<int> digits = digits_array;

  auto i = 9;

  for (auto x : digits) {
    EXPECT_EQ(x, i);
    --i;
  }

  for (auto& x : digits) {
    x = 7;
  }

  for (auto x : digits) {
    EXPECT_EQ(x, 7);
  }

  END_TEST;
}

bool AsBytesTest() {
  BEGIN_TEST;

  int digits_array[] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

  ktl::span<int> digits = digits_array;

  ktl::span<ktl::byte> write_bytes = ktl::as_writable_bytes(digits);

  for (size_t i = 0; i < sizeof(int); ++i) {
    write_bytes[i] = ktl::byte(i);
  }

  ktl::span<const ktl::byte> bytes = ktl::as_bytes(digits);

  for (size_t i = 0; i < sizeof(int); ++i) {
    EXPECT_EQ(bytes[i], ktl::byte(i));
  }

  EXPECT_EQ(digits_array[0], 0x03020100);

  END_TEST;
}

bool DynamicExtentTest() {
  BEGIN_TEST;

  int array_of_ints[3] = {1, 2, 3};

  ktl::span<int> ints = array_of_ints;
  EXPECT_EQ(ints.size(), 3u);

  ktl::span<int> some_ints = { &array_of_ints[0], 2 };
  EXPECT_EQ(some_ints.size(), 2u);

  ktl::span some_more_ints = some_ints;
  EXPECT_EQ(some_more_ints.size(), 2u);

  END_TEST;
}

struct SpannableContainer {
  using value_type = int;
  int* data() { return reinterpret_cast<int*>(0x1234); }
  const int* data() const { return reinterpret_cast<const int*>(0x1234); }
  size_t size() const { return 50; }
};

bool ContainerTest() {
  BEGIN_TEST;

  SpannableContainer writable;

  ktl::span<int> container_span = writable;

  const SpannableContainer not_writable;

  ktl::span<const int> const_container_span = not_writable;

  EXPECT_EQ(container_span.data(), reinterpret_cast<int*>(0x1234));
  EXPECT_EQ(container_span.size(), 50u);

  EXPECT_EQ(const_container_span.data(), reinterpret_cast<int*>(0x1234));
  EXPECT_EQ(const_container_span.size(), 50u);

  END_TEST;
}

struct Incomplete;

static_assert(ktl::is_same_v<ktl::span<Incomplete>::value_type, Incomplete>);
static_assert(ktl::is_same_v<ktl::span<const Incomplete>::value_type, Incomplete>);
static_assert(ktl::is_same_v<ktl::span<volatile Incomplete>::value_type, Incomplete>);

static_assert(ktl::is_same_v<ktl::span<Incomplete>::pointer, Incomplete*>);
static_assert(ktl::is_same_v<ktl::span<const Incomplete>::pointer, const Incomplete*>);

static_assert(ktl::is_same_v<ktl::span<Incomplete>::const_pointer, const Incomplete*>);
static_assert(ktl::is_same_v<ktl::span<const Incomplete>::const_pointer, const Incomplete*>);

static_assert(ktl::is_same_v<ktl::span<Incomplete>::size_type, size_t>);
static_assert(ktl::is_same_v<ktl::span<Incomplete>::difference_type, ptrdiff_t>);

static_assert(ktl::is_same_v<ktl::span<Incomplete>::reference, Incomplete&>);
static_assert(ktl::is_same_v<ktl::span<const Incomplete>::reference, const Incomplete&>);

static_assert(ktl::is_same_v<ktl::span<Incomplete>::const_reference, const Incomplete&>);
static_assert(ktl::is_same_v<ktl::span<const Incomplete>::const_reference, const Incomplete&>);

}  // namespace

UNITTEST_START_TESTCASE(span_tests)
UNITTEST("FrontTest", FrontTest)
UNITTEST("BackTest", BackTest)
UNITTEST("IndexTest", IndexTest)
UNITTEST("SizeEmptyTest", SizeEmptyTest)
UNITTEST("DataReferencesTest", DataReferencesTest)
UNITTEST("IteratorsTest", IteratorsTest)
UNITTEST("AsBytesTest", AsBytesTest)
UNITTEST("DynamicExtentTest", DynamicExtentTest)
UNITTEST("ContainerTest", ContainerTest)
UNITTEST_END_TESTCASE(span_tests, "span", "ktl::span tests")
