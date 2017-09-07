// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>

#include <unittest/unittest.h>

namespace {

// TODO(vtl): We use this because EXPECT_EQ() doesn't work well with functions that return a
// reference.
template <typename T>
T val(const T& x) {
    return x;
}

bool min_test() {
    BEGIN_TEST;

    EXPECT_EQ(val(fbl::min(1, 2)), 1);
    EXPECT_EQ(val(fbl::min(2.1, 1.1)), 1.1);
    EXPECT_EQ(val(fbl::min(1u, 1u)), 1u);

    END_TEST;
}

bool max_test() {
    BEGIN_TEST;

    EXPECT_EQ(val(fbl::max(1, 2)), 2);
    EXPECT_EQ(val(fbl::max(2.1, 1.1)), 2.1);
    EXPECT_EQ(val(fbl::max(1u, 1u)), 1u);

    END_TEST;
}

bool clamp_test() {
    BEGIN_TEST;

    EXPECT_EQ(val(fbl::clamp(1, 2, 6)), 2);
    EXPECT_EQ(val(fbl::clamp(2.1, 2.1, 6.1)), 2.1);
    EXPECT_EQ(val(fbl::clamp(3u, 2u, 6u)), 3u);
    EXPECT_EQ(val(fbl::clamp(6, 2, 6)), 6);
    EXPECT_EQ(val(fbl::clamp(7, 2, 6)), 6);

    EXPECT_EQ(val(fbl::clamp(1, 2, 2)), 2);
    EXPECT_EQ(val(fbl::clamp(2, 2, 2)), 2);
    EXPECT_EQ(val(fbl::clamp(3, 2, 2)), 2);

    END_TEST;
}

bool roundup_test() {
    BEGIN_TEST;

    EXPECT_EQ(fbl::roundup(0u, 1u), 0u);
    EXPECT_EQ(fbl::roundup(0u, 5u), 0u);
    EXPECT_EQ(fbl::roundup(5u, 5u), 5u);

    EXPECT_EQ(fbl::roundup(1u, 6u), 6u);
    EXPECT_EQ(fbl::roundup(6u, 1u), 6u);
    EXPECT_EQ(fbl::roundup(6u, 3u), 6u);
    EXPECT_EQ(fbl::roundup(6u, 4u), 8u);

    EXPECT_EQ(fbl::roundup(15u, 8u), 16u);
    EXPECT_EQ(fbl::roundup(16u, 8u), 16u);
    EXPECT_EQ(fbl::roundup(17u, 8u), 24u);
    EXPECT_EQ(fbl::roundup(123u, 100u), 200u);
    EXPECT_EQ(fbl::roundup(123456u, 1000u), 124000u);

    END_TEST;
}

template <typename T>
bool is_pow2_test() {
    BEGIN_TEST;

    T val = 0;
    EXPECT_FALSE(fbl::is_pow2<T>(val));
    EXPECT_FALSE(fbl::is_pow2<T>(static_cast<T>(val - 1)));

    for (val = 1u; val; val = static_cast<T>(val << 1u)) {
        EXPECT_TRUE(fbl::is_pow2<T>(val));
        EXPECT_FALSE(fbl::is_pow2<T>(static_cast<T>(val - 5u)));
        EXPECT_FALSE(fbl::is_pow2<T>(static_cast<T>(val + 5u)));
    }

    END_TEST;
}

bool lower_bound_test() {
    BEGIN_TEST;

    int* null = nullptr;
    EXPECT_EQ(fbl::lower_bound(null, null, 0), null);

    int value = 5;
    EXPECT_EQ(fbl::lower_bound(&value, &value, 4), &value);
    EXPECT_EQ(fbl::lower_bound(&value, &value, 5), &value);
    EXPECT_EQ(fbl::lower_bound(&value, &value, 6), &value);

    EXPECT_EQ(fbl::lower_bound(&value, &value + 1, 4), &value);
    EXPECT_EQ(fbl::lower_bound(&value, &value + 1, 5), &value);
    EXPECT_EQ(fbl::lower_bound(&value, &value + 1, 6), &value + 1);

    constexpr size_t count = 4;
    int values[count] = { 1, 3, 5, 7 };
    int* first = values;
    int* last = values + count;

    EXPECT_EQ(*fbl::lower_bound(first, last, 0), 1);
    EXPECT_EQ(*fbl::lower_bound(first, last, 1), 1);
    EXPECT_EQ(*fbl::lower_bound(first, last, 2), 3);
    EXPECT_EQ(*fbl::lower_bound(first, last, 3), 3);
    EXPECT_EQ(*fbl::lower_bound(first, last, 4), 5);
    EXPECT_EQ(*fbl::lower_bound(first, last, 5), 5);
    EXPECT_EQ(*fbl::lower_bound(first, last, 6), 7);
    EXPECT_EQ(*fbl::lower_bound(first, last, 7), 7);
    EXPECT_EQ(fbl::lower_bound(first, last, 8), last);

    last = first - 1;
    EXPECT_EQ(fbl::lower_bound(first, first, 0), first);
    EXPECT_EQ(fbl::lower_bound(first, last, 0), last);

    END_TEST;
}

struct LessThan {
    bool operator()(const int& lhs, const int& rhs) {
        return lhs < rhs;
    }
};

bool lower_bound_compare_test() {
    BEGIN_TEST;

    LessThan lessThan;

    int* null = nullptr;
    EXPECT_EQ(fbl::lower_bound(null, null, 0, lessThan), null);

    int value = 5;
    EXPECT_EQ(fbl::lower_bound(&value, &value, 4, lessThan), &value);
    EXPECT_EQ(fbl::lower_bound(&value, &value, 5, lessThan), &value);
    EXPECT_EQ(fbl::lower_bound(&value, &value, 6, lessThan), &value);

    EXPECT_EQ(fbl::lower_bound(&value, &value + 1, 4, lessThan), &value);
    EXPECT_EQ(fbl::lower_bound(&value, &value + 1, 5, lessThan), &value);
    EXPECT_EQ(fbl::lower_bound(&value, &value + 1, 6, lessThan), &value + 1);

    constexpr size_t count = 4;
    int values[count] = { 1, 3, 5, 7 };
    int* first = values;
    int* last = values + count;

    EXPECT_EQ(*fbl::lower_bound(first, last, 0, lessThan), 1);
    EXPECT_EQ(*fbl::lower_bound(first, last, 1, lessThan), 1);
    EXPECT_EQ(*fbl::lower_bound(first, last, 2, lessThan), 3);
    EXPECT_EQ(*fbl::lower_bound(first, last, 3, lessThan), 3);
    EXPECT_EQ(*fbl::lower_bound(first, last, 4, lessThan), 5);
    EXPECT_EQ(*fbl::lower_bound(first, last, 5, lessThan), 5);
    EXPECT_EQ(*fbl::lower_bound(first, last, 6, lessThan), 7);
    EXPECT_EQ(*fbl::lower_bound(first, last, 7, lessThan), 7);
    EXPECT_EQ(fbl::lower_bound(first, last, 8, lessThan), last);

    last = first - 1;
    EXPECT_EQ(fbl::lower_bound(first, first, 0, lessThan), first);
    EXPECT_EQ(fbl::lower_bound(first, last, 0, lessThan), last);

    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(algorithm_tests)
RUN_NAMED_TEST("min test", min_test)
RUN_NAMED_TEST("max test", max_test)
RUN_NAMED_TEST("clamp test", clamp_test)
RUN_NAMED_TEST("roundup test", roundup_test)
RUN_NAMED_TEST("is_pow2<uint8_t>",  is_pow2_test<uint8_t>)
RUN_NAMED_TEST("is_pow2<uint16_t>", is_pow2_test<uint16_t>)
RUN_NAMED_TEST("is_pow2<uint32_t>", is_pow2_test<uint32_t>)
RUN_NAMED_TEST("is_pow2<uint64_t>", is_pow2_test<uint64_t>)
RUN_NAMED_TEST("is_pow2<size_t>",   is_pow2_test<size_t>)
RUN_NAMED_TEST("lower_bound test", lower_bound_test)
RUN_NAMED_TEST("lower_bound_compare test", lower_bound_compare_test)
END_TEST_CASE(algorithm_tests);
