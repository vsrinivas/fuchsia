// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxtl/algorithm.h>

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

    EXPECT_EQ(val(mxtl::min(1, 2)), 1, "");
    EXPECT_EQ(val(mxtl::min(2.1, 1.1)), 1.1, "");
    EXPECT_EQ(val(mxtl::min(1u, 1u)), 1u, "");

    END_TEST;
}

bool max_test() {
    BEGIN_TEST;

    EXPECT_EQ(val(mxtl::max(1, 2)), 2, "");
    EXPECT_EQ(val(mxtl::max(2.1, 1.1)), 2.1, "");
    EXPECT_EQ(val(mxtl::max(1u, 1u)), 1u, "");

    END_TEST;
}

bool clamp_test() {
    BEGIN_TEST;

    EXPECT_EQ(val(mxtl::clamp(1, 2, 6)), 2, "");
    EXPECT_EQ(val(mxtl::clamp(2.1, 2.1, 6.1)), 2.1, "");
    EXPECT_EQ(val(mxtl::clamp(3u, 2u, 6u)), 3u, "");
    EXPECT_EQ(val(mxtl::clamp(6, 2, 6)), 6, "");
    EXPECT_EQ(val(mxtl::clamp(7, 2, 6)), 6, "");

    EXPECT_EQ(val(mxtl::clamp(1, 2, 2)), 2, "");
    EXPECT_EQ(val(mxtl::clamp(2, 2, 2)), 2, "");
    EXPECT_EQ(val(mxtl::clamp(3, 2, 2)), 2, "");

    END_TEST;
}

template <typename T>
bool is_pow2_test() {
    BEGIN_TEST;

    T val = 0;
    EXPECT_FALSE(mxtl::is_pow2<T>(val), "");
    EXPECT_FALSE(mxtl::is_pow2<T>(static_cast<T>(val - 1)), "");

    for (val = 1u; val; val = static_cast<T>(val << 1u)) {
        EXPECT_TRUE(mxtl::is_pow2<T>(val), "");
        EXPECT_FALSE(mxtl::is_pow2<T>(static_cast<T>(val - 5u)), "");
        EXPECT_FALSE(mxtl::is_pow2<T>(static_cast<T>(val + 5u)), "");
    }

    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(algorithm_tests)
RUN_NAMED_TEST("min test", min_test)
RUN_NAMED_TEST("max test", max_test)
RUN_NAMED_TEST("clamp test", clamp_test)
RUN_NAMED_TEST("is_pow2<uint8_t>",  is_pow2_test<uint8_t>)
RUN_NAMED_TEST("is_pow2<uint16_t>", is_pow2_test<uint16_t>)
RUN_NAMED_TEST("is_pow2<uint32_t>", is_pow2_test<uint32_t>)
RUN_NAMED_TEST("is_pow2<uint64_t>", is_pow2_test<uint64_t>)
RUN_NAMED_TEST("is_pow2<size_t>",   is_pow2_test<size_t>)
END_TEST_CASE(algorithm_tests);
