// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <type_traits>

#include <lib/fit/variant.h>
#include <unittest/unittest.h>

namespace {

struct literal_traits {
    using variant = fit::internal::variant<
        fit::internal::monostate, int, double>;

    static constexpr fit::internal::monostate a_value{};
    static constexpr int b_value = 10;
    static constexpr double c_value = 2.5;
    static constexpr double c2_value = 4.2;

    static variant a, b, c;
    static constexpr variant const_a;
    static constexpr variant const_b{fit::internal::in_place_index<1>,
                                     b_value};
    static constexpr variant const_c{fit::internal::in_place_index<2>,
                                     c_value};
};

literal_traits::variant literal_traits::a;
literal_traits::variant literal_traits::b{fit::internal::in_place_index<1>,
                                          literal_traits::b_value};
literal_traits::variant literal_traits::c{fit::internal::in_place_index<2>,
                                          literal_traits::c_value};

struct complex_traits {
    using variant = fit::internal::variant<
        fit::internal::monostate, int, std::string>;

    static const fit::internal::monostate a_value;
    static const int b_value;
    static const std::string c_value;
    static const std::string c2_value;

    static variant a, b, c;
    static const variant const_a;
    static const variant const_b;
    static const variant const_c;
};

const fit::internal::monostate complex_traits::a_value{};
const int complex_traits::b_value = 10;
const std::string complex_traits::c_value = "test";
const std::string complex_traits::c2_value = "another";

complex_traits::variant complex_traits::a;
complex_traits::variant complex_traits::b{fit::internal::in_place_index<1>,
                                          complex_traits::b_value};
complex_traits::variant complex_traits::c{fit::internal::in_place_index<2>,
                                          complex_traits::c_value};

const complex_traits::variant complex_traits::const_a;
const complex_traits::variant complex_traits::const_b{fit::internal::in_place_index<1>,
                                                      complex_traits::b_value};
const complex_traits::variant complex_traits::const_c{fit::internal::in_place_index<2>,
                                                      complex_traits::c_value};

template <typename T>
bool accessors() {
    BEGIN_TEST;

    EXPECT_EQ(0, T::a.index());
    EXPECT_TRUE(T::a_value == T::a.template get<0>());
    EXPECT_TRUE(T::a_value == T::const_a.template get<0>());

    EXPECT_EQ(1, T::b.index());
    EXPECT_TRUE(T::b_value == T::b.template get<1>());
    EXPECT_TRUE(T::b_value == T::const_b.template get<1>());

    EXPECT_EQ(2, T::c.index());
    EXPECT_TRUE(T::c_value == T::c.template get<2>());
    EXPECT_TRUE(T::c_value == T::const_c.template get<2>());

    END_TEST;
}

template <typename T>
bool copy_move_assign() {
    BEGIN_TEST;

    typename T::variant x;
    EXPECT_EQ(0, x.index());
    EXPECT_TRUE(T::a_value == x.template get<0>());

    x = T::b;
    EXPECT_EQ(1, x.index());
    EXPECT_TRUE(T::b_value == x.template get<1>());

    x.template emplace<2>(T::c_value);
    EXPECT_EQ(2, x.index());
    EXPECT_TRUE(T::c_value == x.template get<2>());

    typename T::variant y(T::b);
    EXPECT_EQ(1, y.index());
    EXPECT_TRUE(T::b_value == y.template get<1>());

    x = std::move(y);
    EXPECT_EQ(1, x.index());
    EXPECT_TRUE(T::b_value == x.template get<1>());

    x = x;
    EXPECT_EQ(1, x.index());
    EXPECT_TRUE(T::b_value == x.template get<1>());

    x = std::move(x);
    EXPECT_EQ(1, x.index());
    EXPECT_TRUE(T::b_value == x.template get<1>());

    x = T::a;
    EXPECT_EQ(0, x.index());
    EXPECT_TRUE(T::a_value == x.template get<0>());

    x = T::c;
    typename T::variant z(std::move(x));
    EXPECT_EQ(2, z.index());
    EXPECT_TRUE(T::c_value == z.template get<2>());

    END_TEST;
}

template <typename T>
bool swapping() {
    BEGIN_TEST;

    typename T::variant x;
    EXPECT_EQ(0, x.index());
    EXPECT_TRUE(T::a_value == x.template get<0>());

    typename T::variant y(T::c);
    y.swap(y);
    EXPECT_EQ(2, y.index());
    EXPECT_TRUE(T::c_value == y.template get<2>());

    x.swap(y);
    EXPECT_EQ(2, x.index());
    EXPECT_TRUE(T::c_value == x.template get<2>());
    EXPECT_EQ(0, y.index());
    EXPECT_TRUE(T::a_value == y.template get<0>());

    y.template emplace<2>(T::c2_value);
    x.swap(y);
    EXPECT_EQ(2, x.index());
    EXPECT_TRUE(T::c2_value == x.template get<2>());
    EXPECT_EQ(2, y.index());
    EXPECT_TRUE(T::c_value == y.template get<2>());

    x = T::b;
    y.swap(x);
    EXPECT_EQ(2, x.index());
    EXPECT_TRUE(T::c_value == x.template get<2>());
    EXPECT_EQ(1, y.index());
    EXPECT_TRUE(T::b_value == y.template get<1>());

    x = T::a;
    y.swap(x);
    EXPECT_EQ(1, x.index());
    EXPECT_TRUE(T::b_value == x.template get<1>());
    EXPECT_EQ(0, y.index());
    EXPECT_TRUE(T::a_value == y.template get<0>());

    END_TEST;
}

// Test constexpr behavior.
static_assert(literal_traits::variant().index() == 0, "");
static_assert(literal_traits::const_a.index() == 0, "");
static_assert(literal_traits::const_a.get<0>() == literal_traits::a_value, "");
static_assert(literal_traits::const_b.index() == 1, "");
static_assert(literal_traits::const_b.get<1>() == literal_traits::b_value, "");
static_assert(literal_traits::const_c.index() == 2, "");
static_assert(literal_traits::const_c.get<2>() == literal_traits::c_value, "");

} // namespace

BEGIN_TEST_CASE(variant_tests)
RUN_TEST(accessors<literal_traits>)
RUN_TEST(accessors<complex_traits>)
RUN_TEST(copy_move_assign<literal_traits>)
RUN_TEST(copy_move_assign<complex_traits>)
RUN_TEST(swapping<literal_traits>)
RUN_TEST(swapping<complex_traits>)
END_TEST_CASE(variant_tests)
