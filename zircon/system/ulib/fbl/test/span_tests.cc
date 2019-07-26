// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/span.h>
#include <unittest/unittest.h>

namespace {

static constexpr int kDigitsArray[] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
static constexpr fbl::Span<const int> kDigits = kDigitsArray;  // T (const &)[N] constructor
static constexpr fbl::Span<const int> kMiddleDigits = kDigits.subspan(3, 4);
static constexpr fbl::Span<const int> kLastDigits = kDigits.subspan(7);
static constexpr fbl::Span<const int> kEmpty = kDigits.subspan(0, 0);
static constexpr fbl::Span<const int> kDefault;  // default constructor

bool front_test() {
  BEGIN_TEST;

  EXPECT_EQ(kDigits.front(), 9);
  EXPECT_EQ(kMiddleDigits.front(), 6);
  EXPECT_EQ(kLastDigits.front(), 2);

  END_TEST;
}

bool back_test() {
  BEGIN_TEST;

  EXPECT_EQ(kDigits.back(), 0);
  EXPECT_EQ(kMiddleDigits.back(), 3);
  EXPECT_EQ(kLastDigits.back(), 0);

  END_TEST;
}

bool index_test() {
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

bool size_empty_test() {
  BEGIN_TEST;

  EXPECT_EQ(kDigits.size(), 10);
  EXPECT_EQ(kMiddleDigits.size(), 4);
  EXPECT_EQ(kLastDigits.size(), 3);
  EXPECT_EQ(kEmpty.size(), 0);
  EXPECT_EQ(kDefault.size(), 0);

  EXPECT_FALSE(kDigits.empty());
  EXPECT_FALSE(kMiddleDigits.empty());
  EXPECT_FALSE(kLastDigits.empty());
  EXPECT_TRUE(kEmpty.empty());
  EXPECT_TRUE(kDefault.empty());

  EXPECT_EQ(kDigits.size_bytes(), 10 * sizeof(int));
  EXPECT_EQ(kMiddleDigits.size_bytes(), 4 * sizeof(int));
  EXPECT_EQ(kLastDigits.size_bytes(), 3 * sizeof(int));
  EXPECT_EQ(kEmpty.size_bytes(), 0);
  EXPECT_EQ(kDefault.size_bytes(), 0);

  END_TEST;
}

bool data_references_test() {
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

bool iterators_test() {
  BEGIN_TEST;

  int digits_array[] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

  fbl::Span<int> digits = digits_array;

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

bool as_bytes_test() {
  BEGIN_TEST;

  int digits_array[] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

  fbl::Span<int> digits = digits_array;

  fbl::Span<std::byte> write_bytes = fbl::as_writable_bytes(digits);

  for (size_t i = 0; i < sizeof(int); ++i) {
    write_bytes[i] = std::byte(0x00);
  }

  fbl::Span<const std::byte> bytes = fbl::as_bytes(digits);

  for (size_t i = 0; i < sizeof(int); ++i) {
    EXPECT_EQ(bytes[i], std::byte(0x00));
  }

  EXPECT_EQ(digits_array[0], 0);

  END_TEST;
}

struct SpannableContainer {
  using value_type = int;
  int* data() { return reinterpret_cast<int*>(0x1234); }
  const int* data() const { return reinterpret_cast<const int*>(0x1234); }
  size_t size() const { return 50; }
};

bool container_test() {
  BEGIN_TEST;

  SpannableContainer writable;

  fbl::Span<int> container_span = writable;

  const SpannableContainer not_writable;

  fbl::Span<const int> const_container_span = not_writable;

  EXPECT_EQ(container_span.data(), reinterpret_cast<int*>(0x1234));
  EXPECT_EQ(container_span.size(), 50);

  EXPECT_EQ(const_container_span.data(), reinterpret_cast<int*>(0x1234));
  EXPECT_EQ(const_container_span.size(), 50);

  END_TEST;
}

struct Incomplete;

static_assert(std::is_same_v<fbl::Span<Incomplete>::value_type, Incomplete>);
static_assert(std::is_same_v<fbl::Span<const Incomplete>::value_type, Incomplete>);
static_assert(std::is_same_v<fbl::Span<volatile Incomplete>::value_type, Incomplete>);

static_assert(std::is_same_v<fbl::Span<Incomplete>::pointer, Incomplete*>);
static_assert(std::is_same_v<fbl::Span<const Incomplete>::pointer, const Incomplete*>);

static_assert(std::is_same_v<fbl::Span<Incomplete>::const_pointer, const Incomplete*>);
static_assert(std::is_same_v<fbl::Span<const Incomplete>::const_pointer, const Incomplete*>);

static_assert(std::is_same_v<fbl::Span<Incomplete>::index_type, size_t>);
static_assert(std::is_same_v<fbl::Span<Incomplete>::difference_type, ptrdiff_t>);

static_assert(std::is_same_v<fbl::Span<Incomplete>::reference, Incomplete&>);
static_assert(std::is_same_v<fbl::Span<const Incomplete>::reference, const Incomplete&>);

static_assert(std::is_same_v<fbl::Span<Incomplete>::const_reference, const Incomplete&>);
static_assert(std::is_same_v<fbl::Span<const Incomplete>::const_reference, const Incomplete&>);

}  // namespace

BEGIN_TEST_CASE(span_tests)
RUN_TEST(front_test)
RUN_TEST(back_test)
RUN_TEST(index_test)
RUN_TEST(size_empty_test)
RUN_TEST(data_references_test)
RUN_TEST(iterators_test)
RUN_TEST(as_bytes_test)
RUN_TEST(container_test)
END_TEST_CASE(span_tests)
