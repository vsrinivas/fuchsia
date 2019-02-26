// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ffl/fixed.h>

#include <cstdint>
#include <iostream>
#include <type_traits>
#include <unittest/unittest.h>

namespace {

using ffl::Fixed;
using ffl::FromRatio;
using ffl::FromRaw;
using ffl::ToResolution;

template <typename Integer, size_t FractionalBits>
constexpr bool IsSaturated(Fixed<Integer, FractionalBits> value) {
    using Format = typename Fixed<Integer, FractionalBits>::Format;
    return value.raw_value() == Format::Min || value.raw_value() == Format::Max;
}

template <typename Intermediate, typename Integer>
constexpr bool ShouldSaturate(Intermediate value, Integer min_value, Integer max_value) {
    return value < min_value || value > max_value;
}

template <typename Integer, size_t FractionalBits>
std::ostream& operator<<(std::ostream& stream, Fixed<Integer, FractionalBits> value) {
    using Format = typename Fixed<Integer, FractionalBits>::Format;
    const double double_value = static_cast<double>(value.raw_value()) / Format::Power;
    stream << double_value << " (" << std::hex << static_cast<unsigned>(value.raw_value()) << ")";
    stream << std::dec;
    return stream;
}

template <typename Integer, size_t FractionalBits>
bool integer_arithmetic() {
    BEGIN_TEST;

    // Alias for the target fixed-point type to test.
    using Q = Fixed<Integer, FractionalBits>;

    // Select an intermediate fixed-point type to handle multiplication and
    // division results. Just use the largest intermediate type we expect to
    // test to avoid a complex table. This is mostly just to make -Wconversion
    // happy.
    using Intermediate = std::conditional_t<std::is_signed_v<Integer>, int64_t, uint64_t>;
    using QI = Fixed<Intermediate, FractionalBits + 2>;

    const Integer kMin = Q::Format::IntegralMin;
    const Integer kMax = Q::Format::IntegralMax;

    // TODO(eieio): Change this test to cover values near min, max, and zero
    // instead of the full integer range for the fixed point type.
    static_assert(kMin * kMax <= 255 * 255, "Testing this integer range will take too long!");

    // Check that fixed point arithmetic over the range of integers produces the
    // same result, or similar within expected deviation, as integer arithmetic.
    for (Intermediate a = kMin; a <= kMax; a++) {
        for (Intermediate b = kMin; b <= kMax; b++) {
            const Integer int_a = static_cast<Integer>(a);
            const Integer int_b = static_cast<Integer>(b);

            const Q fixed_a{int_a};
            const Q fixed_b{int_b};

            // Compare sums and differences between plain integers and fixed-
            // point values over the integers. Values that do not saturate
            // should be the same, whereas most saturated values should be
            // different unless the saturated value happens to round up to the
            // same value as plain integer arithmetic in the larger type.
            const Intermediate int_sum = int_a + int_b;
            const Q fixed_sum = fixed_a + fixed_b;
            if (ShouldSaturate(int_sum, kMin, kMax)) {
                EXPECT_TRUE(IsSaturated(fixed_sum));
                EXPECT_TRUE(int_sum != fixed_sum || int_sum == fixed_sum.Round());
            } else {
                EXPECT_TRUE(int_sum == fixed_sum);
            }

            const Intermediate int_difference = int_a - int_b;
            const Q fixed_difference = fixed_a - fixed_b;
            if (ShouldSaturate(int_difference, kMin, kMax)) {
                EXPECT_TRUE(IsSaturated(fixed_difference));
                EXPECT_TRUE(int_difference != fixed_difference ||
                            int_difference == fixed_difference.Round());
            } else {
                EXPECT_TRUE(int_difference == fixed_difference);
            }

            // Compare products between plain integers and fixed-point values
            // over the integers. Products should be the same over the larger
            // types.
            const Intermediate int_product = int_a * int_b;
            const QI fixed_product = fixed_a * fixed_b;
            EXPECT_TRUE(int_product == fixed_product);

            // Compare quotients between plain integers and fixed-point values
            // over the integers. Convergent rounding results in at most a +-1
            // difference.
            if (int_b != 0) {
                const Intermediate int_quotient = int_a / int_b;
                const QI fixed_quotient = fixed_a / fixed_b;
                EXPECT_TRUE((int_quotient + 1) == fixed_quotient ||
                            (int_quotient + 0) == fixed_quotient ||
                            (int_quotient - 1) == fixed_quotient);
            }
        }
    }

    END_TEST;
}

bool ceiling_test() {
    BEGIN_TEST;

    EXPECT_EQ(1, (Fixed<int, 0>{1}.Ceiling()));
    EXPECT_EQ(1, (Fixed<int, 1>{FromRatio(1, 2)}.Ceiling()));
    EXPECT_EQ(1, (Fixed<int, 2>{FromRatio(1, 2)}.Ceiling()));
    EXPECT_EQ(1, (Fixed<int, 2>{FromRatio(1, 4)}.Ceiling()));
    EXPECT_EQ(0, (Fixed<int, 1>{FromRatio(-1, 2)}.Ceiling()));
    EXPECT_EQ(0, (Fixed<int, 2>{FromRatio(-1, 2)}.Ceiling()));
    EXPECT_EQ(0, (Fixed<int, 2>{FromRatio(-1, 4)}.Ceiling()));
    EXPECT_EQ(-1, (Fixed<int, 0>{-1}.Ceiling()));

    END_TEST;
}

bool floor_test() {
    BEGIN_TEST;

    EXPECT_EQ(1, (Fixed<int, 0>{1}.Floor()));
    EXPECT_EQ(0, (Fixed<int, 1>{FromRatio(1, 2)}.Floor()));
    EXPECT_EQ(0, (Fixed<int, 2>{FromRatio(1, 2)}.Floor()));
    EXPECT_EQ(0, (Fixed<int, 2>{FromRatio(1, 4)}.Floor()));
    EXPECT_EQ(-1, (Fixed<int, 1>{FromRatio(-1, 2)}.Floor()));
    EXPECT_EQ(-1, (Fixed<int, 2>{FromRatio(-1, 2)}.Floor()));
    EXPECT_EQ(-1, (Fixed<int, 2>{FromRatio(-1, 4)}.Floor()));
    EXPECT_EQ(-1, (Fixed<int, 0>{-1}.Floor()));

    END_TEST;
}

// TODO(ZX-3777): Port the rest of the local gtest tests.

} // anonymous namespace

BEGIN_TEST_CASE(ffl_tests)
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<int8_t, 0>))
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<int8_t, 1>))
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<int8_t, 2>))
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<int8_t, 3>))
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<int8_t, 4>))
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<int8_t, 5>))
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<int8_t, 6>))
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<int8_t, 7>))

RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<uint8_t, 0>))
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<uint8_t, 1>))
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<uint8_t, 2>))
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<uint8_t, 3>))
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<uint8_t, 4>))
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<uint8_t, 5>))
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<uint8_t, 6>))
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<uint8_t, 7>))
RUN_NAMED_TEST("integer arithmetic", (integer_arithmetic<uint8_t, 8>))

RUN_NAMED_TEST("ceiling test", ceiling_test)
RUN_NAMED_TEST("floor test", floor_test)
END_TEST_CASE(ffl_tests);
