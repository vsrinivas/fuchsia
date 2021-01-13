// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/type_traits.h>

#include <functional>
#include <type_traits>

#include <gtest/gtest.h>

namespace {

#if __cpp_lib_void_t >= 201411L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)
TEST(VoidTraitsTest, IsAliasForStd) {
  static_assert(std::is_same_v<cpp17::void_t<>, std::void_t<>>);
}
#endif

#if __cpp_lib_logical_traits >= 201510L
#endif

TEST(VoidTraitsTest, TypeDecaysToVoid) {
  static_assert(std::is_same_v<cpp17::void_t<>, void>, "");
  static_assert(std::is_same_v<cpp17::void_t<int>, void>, "");
  static_assert(std::is_same_v<cpp17::void_t<int, int>, void>, "");
}

TEST(LogicalTraitsTest, ConjunctionIsOk) {
  static_assert(cpp17::conjunction_v<> == true, "");
  static_assert(cpp17::conjunction_v<std::false_type> == false, "");
  static_assert(cpp17::conjunction_v<std::true_type> == true, "");
  static_assert(cpp17::conjunction_v<std::false_type, std::false_type> == false, "");
  static_assert(cpp17::conjunction_v<std::false_type, std::true_type> == false, "");
  static_assert(cpp17::conjunction_v<std::true_type, std::false_type> == false, "");
  static_assert(cpp17::conjunction_v<std::true_type, std::true_type> == true, "");
}

TEST(LogicalTraitsTest, DisjunctionIsOk) {
  static_assert(cpp17::disjunction_v<> == false, "");
  static_assert(cpp17::disjunction_v<std::false_type> == false, "");
  static_assert(cpp17::disjunction_v<std::true_type> == true, "");
  static_assert(cpp17::disjunction_v<std::false_type, std::false_type> == false, "");
  static_assert(cpp17::disjunction_v<std::false_type, std::true_type> == true, "");
  static_assert(cpp17::disjunction_v<std::true_type, std::false_type> == true, "");
  static_assert(cpp17::disjunction_v<std::true_type, std::true_type> == true, "");
}

TEST(LogicalTraitsTest, NegationIsOk) {
  static_assert(cpp17::negation_v<std::false_type> == true, "");
  static_assert(cpp17::negation_v<std::true_type> == false, "");
}

#if __cpp_lib_logical_traits >= 201510L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)
TEST(LogicalTraitsTest, IsAliasForStd) {
  static_assert(
      std::is_same_v<cpp17::conjunction<std::true_type>, std::conjunction<std::true_type>>);
  static_assert(
      std::is_same_v<cpp17::conjunction<std::false_type>, std::conjunction<std::false_type>>);
  static_assert(std::is_same_v<cpp17::conjunction<std::false_type, std::true_type>,
                               std::conjunction<std::false_type, std::true_type>>);
  static_assert(std::is_same_v<cpp17::conjunction<std::true_type, std::false_type>,
                               std::conjunction<std::true_type, std::false_type>>);
  static_assert(std::is_same_v<cpp17::conjunction<std::true_type, std::true_type>,
                               std::conjunction<std::true_type, std::true_type>>);
  static_assert(std::is_same_v<cpp17::conjunction<std::false_type, std::false_type>,
                               std::conjunction<std::false_type, std::false_type>>);

  static_assert(
      std::is_same_v<cpp17::conjunction<std::true_type>, std::conjunction<std::true_type>>);
  static_assert(
      std::is_same_v<cpp17::conjunction<std::false_type>, std::conjunction<std::false_type>>);
  static_assert(std::is_same_v<cpp17::disjunction<std::false_type, std::true_type>,
                               std::disjunction<std::false_type, std::true_type>>);
  static_assert(std::is_same_v<cpp17::disjunction<std::true_type, std::false_type>,
                               std::disjunction<std::true_type, std::false_type>>);
  static_assert(std::is_same_v<cpp17::disjunction<std::true_type, std::true_type>,
                               std::disjunction<std::true_type, std::true_type>>);
  static_assert(std::is_same_v<cpp17::disjunction<std::false_type, std::false_type>,
                               std::disjunction<std::false_type, std::false_type>>);

  static_assert(std::is_same_v<cpp17::negation<std::true_type>, std::negation<std::true_type>>);
  static_assert(std::is_same_v<cpp17::negation<std::false_type>, std::negation<std::false_type>>);
}
#endif

}  // namespace
