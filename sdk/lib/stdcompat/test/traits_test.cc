// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/type_traits.h>

#include <functional>
#include <type_traits>

#include "gtest.h"

namespace {

#if defined(__cpp_lib_void_t) && __cpp_lib_void_t >= 201411L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(VoidTraitsTest, IsAliasForStd) {
  static_assert(std::is_same_v<cpp17::void_t<>, std::void_t<>>);
}

#endif  // __cpp_lib_void_t >= 201411L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

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

#if defined(__cpp_lib_logical_traits) && __cpp_lib_logical_traits >= 201510L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

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

#endif  // __cpp_lib_logical_traits >= 201510L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

#if defined(__cpp_lib_type_trait_variable_templates) && \
    __cpp_lib_type_trait_variable_templates >= 201510L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

template <typename T>
static constexpr bool my_is_array_v = std::is_array<T>::value;

TEST(VariableTemplatesTest, IsAliasForStd) {
  // Don't need std::addressof because it's clear these are fundamental types.
  // Specializations of these templates are UB so we don't need to check for those.

  // The addresses won't be equal for a non-alias, even if defined the same way:
  static_assert(&my_is_array_v<int> != &std::is_array_v<int>);

  // Now we can check all of these for one instantation, which is all we really need.
  static_assert(&cpp17::is_array_v<int> == &std::is_array_v<int>);
  static_assert(&cpp17::is_class_v<int> == &std::is_class_v<int>);
  static_assert(&cpp17::is_enum_v<int> == &std::is_enum_v<int>);
  static_assert(&cpp17::is_floating_point_v<int> == &std::is_floating_point_v<int>);
  static_assert(&cpp17::is_function_v<int> == &std::is_function_v<int>);
  static_assert(&cpp17::is_integral_v<int> == &std::is_integral_v<int>);
  static_assert(&cpp17::is_lvalue_reference_v<int> == &std::is_lvalue_reference_v<int>);
  static_assert(&cpp17::is_member_function_pointer_v<int> ==
                &std::is_member_function_pointer_v<int>);
  static_assert(&cpp17::is_member_object_pointer_v<int> == &std::is_member_object_pointer_v<int>);
  static_assert(&cpp17::is_null_pointer_v<int> == &std::is_null_pointer_v<int>);
  static_assert(&cpp17::is_pointer_v<int> == &std::is_pointer_v<int>);
  static_assert(&cpp17::is_rvalue_reference_v<int> == &std::is_rvalue_reference_v<int>);
  static_assert(&cpp17::is_union_v<int> == &std::is_union_v<int>);
  static_assert(&cpp17::is_void_v<int> == &std::is_void_v<int>);

  static_assert(&cpp17::is_arithmetic_v<int> == &std::is_arithmetic_v<int>);
  static_assert(&cpp17::is_compound_v<int> == &std::is_compound_v<int>);
  static_assert(&cpp17::is_fundamental_v<int> == &std::is_fundamental_v<int>);
  static_assert(&cpp17::is_member_pointer_v<int> == &std::is_member_pointer_v<int>);
  static_assert(&cpp17::is_object_v<int> == &std::is_object_v<int>);
  static_assert(&cpp17::is_reference_v<int> == &std::is_reference_v<int>);
  static_assert(&cpp17::is_scalar_v<int> == &std::is_scalar_v<int>);

  static_assert(&cpp17::is_abstract_v<int> == &std::is_abstract_v<int>);
  static_assert(&cpp17::is_const_v<int> == &std::is_const_v<int>);
  static_assert(&cpp17::is_empty_v<int> == &std::is_empty_v<int>);
  static_assert(&cpp17::is_final_v<int> == &std::is_final_v<int>);
  static_assert(&cpp17::is_pod_v<int> == &std::is_pod_v<int>);
  static_assert(&cpp17::is_polymorphic_v<int> == &std::is_polymorphic_v<int>);
  static_assert(&cpp17::is_signed_v<int> == &std::is_signed_v<int>);
  static_assert(&cpp17::is_standard_layout_v<int> == &std::is_standard_layout_v<int>);
  static_assert(&cpp17::is_trivial_v<int> == &std::is_trivial_v<int>);
  static_assert(&cpp17::is_trivially_copyable_v<int> == &std::is_trivially_copyable_v<int>);
  static_assert(&cpp17::is_unsigned_v<int> == &std::is_unsigned_v<int>);
  static_assert(&cpp17::is_volatile_v<int> == &std::is_volatile_v<int>);

  static_assert(&cpp17::is_constructible_v<int> == &std::is_constructible_v<int>);
  static_assert(&cpp17::is_nothrow_constructible_v<int> == &std::is_nothrow_constructible_v<int>);
  static_assert(&cpp17::is_trivially_constructible_v<int> ==
                &std::is_trivially_constructible_v<int>);

  static_assert(&cpp17::is_default_constructible_v<int> == &std::is_default_constructible_v<int>);
  static_assert(&cpp17::is_nothrow_default_constructible_v<int> ==
                &std::is_nothrow_default_constructible_v<int>);
  static_assert(&cpp17::is_trivially_default_constructible_v<int> ==
                &std::is_trivially_default_constructible_v<int>);

  static_assert(&cpp17::is_copy_constructible_v<int> == &std::is_copy_constructible_v<int>);
  static_assert(&cpp17::is_nothrow_copy_constructible_v<int> ==
                &std::is_nothrow_copy_constructible_v<int>);
  static_assert(&cpp17::is_trivially_copy_constructible_v<int> ==
                &std::is_trivially_copy_constructible_v<int>);

  static_assert(&cpp17::is_move_constructible_v<int> == &std::is_move_constructible_v<int>);
  static_assert(&cpp17::is_nothrow_move_constructible_v<int> ==
                &std::is_nothrow_move_constructible_v<int>);
  static_assert(&cpp17::is_trivially_move_constructible_v<int> ==
                &std::is_trivially_move_constructible_v<int>);

  static_assert(&cpp17::is_assignable_v<int, int> == &std::is_assignable_v<int, int>);
  static_assert(&cpp17::is_nothrow_assignable_v<int, int> ==
                &std::is_nothrow_assignable_v<int, int>);
  static_assert(&cpp17::is_trivially_assignable_v<int, int> ==
                &std::is_trivially_assignable_v<int, int>);

  static_assert(&cpp17::is_copy_assignable_v<int> == &std::is_copy_assignable_v<int>);
  static_assert(&cpp17::is_nothrow_copy_assignable_v<int> ==
                &std::is_nothrow_copy_assignable_v<int>);
  static_assert(&cpp17::is_trivially_copy_assignable_v<int> ==
                &std::is_trivially_copy_assignable_v<int>);

  static_assert(&cpp17::is_move_assignable_v<int> == &std::is_move_assignable_v<int>);
  static_assert(&cpp17::is_nothrow_move_assignable_v<int> ==
                &std::is_nothrow_move_assignable_v<int>);
  static_assert(&cpp17::is_trivially_move_assignable_v<int> ==
                &std::is_trivially_move_assignable_v<int>);

  static_assert(&cpp17::is_destructible_v<int> == &std::is_destructible_v<int>);
  static_assert(&cpp17::is_nothrow_destructible_v<int> == &std::is_nothrow_destructible_v<int>);
  static_assert(&cpp17::is_trivially_destructible_v<int> == &std::is_trivially_destructible_v<int>);

  static_assert(&cpp17::has_virtual_destructor_v<int> == &std::has_virtual_destructor_v<int>);

  static_assert(&cpp17::alignment_of_v<int> == &std::alignment_of_v<int>);
  static_assert(&cpp17::extent_v<int[42]> == &std::extent_v<int[42]>);
  static_assert(&cpp17::rank_v<int[42]> == &std::rank_v<int[42]>);

  static_assert(&cpp17::is_base_of_v<int, int> == &std::is_base_of_v<int, int>);
  static_assert(&cpp17::is_convertible_v<int, int> == &std::is_convertible_v<int, int>);
  static_assert(&cpp17::is_same_v<int, int> == &std::is_same_v<int, int>);
}

#endif  // __cpp_lib_type_trait_variable_templates >= 201510L &&
        // !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(ArrayTraitsTest, BoundedUnboundedArrayIsOk) {
  static_assert(cpp20::is_bounded_array_v<void> == false, "");
  static_assert(cpp20::is_bounded_array_v<int> == false, "");
  static_assert(cpp20::is_bounded_array_v<int*> == false, "");
  static_assert(cpp20::is_bounded_array_v<int(*)[]> == false, "");
  static_assert(cpp20::is_bounded_array_v<int(&)[]> == false, "");
  static_assert(cpp20::is_bounded_array_v<int(&&)[]> == false, "");
  static_assert(cpp20::is_bounded_array_v<int(*)[10]> == false, "");
  static_assert(cpp20::is_bounded_array_v<int(&)[10]> == false, "");
  static_assert(cpp20::is_bounded_array_v<int(&&)[10]> == false, "");
  static_assert(cpp20::is_bounded_array_v<int[10]> == true, "");
  static_assert(cpp20::is_bounded_array_v<int[]> == false, "");

  static_assert(cpp20::is_unbounded_array_v<void> == false, "");
  static_assert(cpp20::is_unbounded_array_v<int> == false, "");
  static_assert(cpp20::is_unbounded_array_v<int*> == false, "");
  static_assert(cpp20::is_unbounded_array_v<int(*)[]> == false, "");
  static_assert(cpp20::is_unbounded_array_v<int(&)[]> == false, "");
  static_assert(cpp20::is_unbounded_array_v<int(&&)[]> == false, "");
  static_assert(cpp20::is_unbounded_array_v<int(*)[10]> == false, "");
  static_assert(cpp20::is_unbounded_array_v<int(&)[10]> == false, "");
  static_assert(cpp20::is_unbounded_array_v<int(&&)[10]> == false, "");
  static_assert(cpp20::is_unbounded_array_v<int[10]> == false, "");
  static_assert(cpp20::is_unbounded_array_v<int[]> == true, "");
}

#if defined(__cpp_lib_bounded_array_traits) && __cpp_lib_bounded_array_traits >= 201902L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(ArrayTraitsTest, IsAliasForStd) {
  static_assert(std::is_same_v<cpp20::is_bounded_array<void>, std::is_bounded_array<void>>);
  static_assert(std::is_same_v<cpp20::is_bounded_array<int[]>, std::is_bounded_array<int[]>>);
  static_assert(std::is_same_v<cpp20::is_bounded_array<int[10]>, std::is_bounded_array<int[10]>>);

  static_assert(std::is_same_v<cpp20::is_unbounded_array<void>, std::is_unbounded_array<void>>);
  static_assert(std::is_same_v<cpp20::is_unbounded_array<int[]>, std::is_unbounded_array<int[]>>);
  static_assert(
      std::is_same_v<cpp20::is_unbounded_array<int[10]>, std::is_unbounded_array<int[10]>>);
}

#endif  // __cpp_lib_bounded_array_traits >= 201902L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(RemoveCvrefTest, RemoveCvrefIsOk) {
  static_assert(std::is_same_v<cpp20::remove_cvref_t<void>, void>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref_t<const volatile void>, void>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref_t<int>, int>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref_t<int*>, int*>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref_t<int*&>, int*>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref_t<int*&&>, int*>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref_t<int&>, int>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref_t<int&&>, int>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref_t<const int>, int>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref_t<const int&>, int>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref_t<const int&&>, int>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref_t<const volatile int&&>, int>, "");
}

#if defined(__cpp_lib_remove_cvref) && __cpp_lib_remove_cvref >= 201711L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(RemoveCvrefTest, IsAliasForStd) {
  static_assert(std::is_same_v<cpp20::remove_cvref<void>, std::remove_cvref<void>>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref<int>, std::remove_cvref<int>>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref<int*>, std::remove_cvref<int*>>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref<int&>, std::remove_cvref<int&>>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref<int&&>, std::remove_cvref<int&&>>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref<const int>, std::remove_cvref<const int>>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref<const int&>, std::remove_cvref<const int&>>, "");
  static_assert(std::is_same_v<cpp20::remove_cvref<const int&&>, std::remove_cvref<const int&&>>,
                "");
  static_assert(std::is_same_v<cpp20::remove_cvref<const volatile int&&>,
                               std::remove_cvref<const volatile int&&>>,
                "");
}

#endif  // __cpp_lib_remove_cvref >= 201711L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(TypeIdentityTest, TypeIdentityIsOk) {
  static_assert(std::is_same_v<cpp20::type_identity_t<void>, void>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<int>, int>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<int*>, int*>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<int*&>, int*&>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<int*&&>, int*&&>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<int&>, int&>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<int&&>, int&&>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<const int>, const int>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<const int&>, const int&>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<const int&&>, const int&&>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<const volatile int&&>, const volatile int&&>,
                "");
}

#if defined(__cpp_lib_type_identity) && __cpp_lib_type_identity >= 201806L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(TypeIdentityTest, IsAliasForStd) {
  static_assert(std::is_same_v<cpp20::type_identity_t<void>, std::type_identity_t<void>>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<int>, std::type_identity_t<int>>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<int*>, std::type_identity_t<int*>>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<int*&>, std::type_identity_t<int*&>>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<int*&&>, std::type_identity_t<int*&&>>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<int&>, std::type_identity_t<int&>>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<int&&>, std::type_identity_t<int&&>>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<const int>, std::type_identity_t<const int>>,
                "");
  static_assert(
      std::is_same_v<cpp20::type_identity_t<const int&>, std::type_identity_t<const int&>>, "");
  static_assert(
      std::is_same_v<cpp20::type_identity_t<const int&&>, std::type_identity_t<const int&&>>, "");
  static_assert(std::is_same_v<cpp20::type_identity_t<const volatile int&&>,
                               std::type_identity_t<const volatile int&&>>,
                "");
}

#endif  // __cpp_lib_type_identity >= 201806L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

struct static_constants {
  [[maybe_unused]] static constexpr int kRed = 0;
  [[maybe_unused]] static constexpr int kGreen = 1;
  [[maybe_unused]] static constexpr int kBlue = 2;
};
enum color { kRed, kGreen, kBlue };
enum class scoped_color { kRed, kGreen, kBlue };
enum class scoped_color_char : char { kRed, kGreen, kBlue };

TEST(ScopedEnumTest, ScopedEnumIsOk) {
  static_assert(cpp23::is_scoped_enum_v<void> == false, "");
  static_assert(cpp23::is_scoped_enum_v<int> == false, "");
  static_assert(cpp23::is_scoped_enum_v<static_constants> == false, "");
  static_assert(cpp23::is_scoped_enum_v<color> == false, "");
  static_assert(cpp23::is_scoped_enum_v<scoped_color> == true, "");
  static_assert(cpp23::is_scoped_enum_v<scoped_color_char> == true, "");
}

#if defined(__cpp_lib_is_scoped_enum) && __cpp_lib_is_scoped_enum >= 202011L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(ScopedEnumTest, IsAliasForStd) {
  static_assert(std::is_same_v<cpp23::is_scoped_enum<void>, std::is_scoped_enum<void>>, "");
  static_assert(std::is_same_v<cpp23::is_scoped_enum<int>, std::is_scoped_enum<int>>, "");
  static_assert(std::is_same_v<cpp23::is_scoped_enum<static_constants>,
                               std::is_scoped_enum<static_constants>>,
                "");
  static_assert(std::is_same_v<cpp23::is_scoped_enum<color>, std::is_scoped_enum<color>>, "");
  static_assert(
      std::is_same_v<cpp23::is_scoped_enum<scoped_color>, std::is_scoped_enum<scoped_color>>, "");
  static_assert(std::is_same_v<cpp23::is_scoped_enum<scoped_color_char>,
                               std::is_scoped_enum<scoped_color_char>>,
                "");
}

#endif  // __cpp_lib_is_scoped_enum >= 202011L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

struct nothing {};

struct pair {
  int first;
  int second;
};

struct member_function {
  void f();
};

struct virtual_function {
  virtual void f();
};

struct user_defined_constructor {
  constexpr user_defined_constructor() {}
};

TEST(IsAggregateTest, AggregateIsOk) {
  static_assert(cpp17::is_aggregate_v<void> == false, "");
  static_assert(cpp17::is_aggregate_v<int> == false, "");
  static_assert(cpp17::is_aggregate_v<nothing> == true, "");
  static_assert(cpp17::is_aggregate_v<pair> == true, "");
  static_assert(cpp17::is_aggregate_v<member_function> == true, "");
  static_assert(cpp17::is_aggregate_v<virtual_function> == false, "");
  static_assert(cpp17::is_aggregate_v<user_defined_constructor> == false, "");
}

#if defined(__cpp_lib_is_aggregate) && __cpp_lib_is_aggregate >= 201703L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(IsAggregateTest, IsAliasForStd) {
  static_assert(std::is_same_v<cpp17::is_aggregate<void>, std::is_aggregate<void>>, "");
  static_assert(std::is_same_v<cpp17::is_aggregate<int>, std::is_aggregate<int>>, "");
  static_assert(std::is_same_v<cpp17::is_aggregate<nothing>, std::is_aggregate<nothing>>, "");
  static_assert(std::is_same_v<cpp17::is_aggregate<pair>, std::is_aggregate<pair>>, "");
  static_assert(
      std::is_same_v<cpp17::is_aggregate<member_function>, std::is_aggregate<member_function>>, "");
  static_assert(
      std::is_same_v<cpp17::is_aggregate<virtual_function>, std::is_aggregate<virtual_function>>,
      "");
  static_assert(std::is_same_v<cpp17::is_aggregate<user_defined_constructor>,
                               std::is_aggregate<user_defined_constructor>>,
                "");
}

#endif  // __cpp_lib_is_aggregate >= 201703L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

constexpr auto noop = [] {};
constexpr auto take_all = [](auto&&...) {};
constexpr auto lambda_add_one = [](int i) { return i + 1; };

[[maybe_unused]] int func_add_one(int i) { return i + 1; }

struct member_pointers {
  virtual int pmf_add_one(int i) { return i + 1; }
  int (*pmd_add_one)(int) = func_add_one;
};

struct liar : member_pointers {
  int pmf_add_one(int i) override { return i + 2; }
};

struct independent_liar {
  int pmf_add_one(int i) { return i + 3; }
};

TEST(InvokeTraitsTest, IsInvocable) {
  static_assert(cpp17::is_invocable_v<decltype(noop)> == true, "");
  static_assert(cpp17::is_invocable_v<decltype(lambda_add_one), int> == true, "");
  static_assert(cpp17::is_invocable_v<decltype(func_add_one), int> == true, "");

  static_assert(cpp17::is_invocable_v<void> == false, "");
  static_assert(cpp17::is_invocable_v<void*> == false, "");
  static_assert(cpp17::is_invocable_v<char*> == false, "");
  static_assert(cpp17::is_invocable_v<int, int> == false, "");
  static_assert(cpp17::is_invocable_v<member_pointers, int> == false, "");

  struct incomplete;
  static_assert(cpp17::is_invocable_v<decltype(take_all), incomplete> == true, "");
}

TEST(InvokeTraitsTest, PmfIsInvocable) {
  static_assert(
      cpp17::is_invocable_v<decltype(&member_pointers::pmf_add_one), member_pointers*, int> == true,
      "");
  static_assert(
      cpp17::is_invocable_v<decltype(&member_pointers::pmf_add_one), member_pointers&, int> == true,
      "");
  static_assert(
      cpp17::is_invocable_v<decltype(&member_pointers::pmf_add_one), member_pointers&&, int> ==
          true,
      "");
  static_assert(
      cpp17::is_invocable_v<decltype(&member_pointers::pmf_add_one), member_pointers, int> == true,
      "");
  static_assert(cpp17::is_invocable_v<decltype(&member_pointers::pmf_add_one),
                                      std::reference_wrapper<member_pointers>, int> == true,
                "");
  static_assert(cpp17::is_invocable_v<decltype(&member_pointers::pmf_add_one),
                                      std::unique_ptr<member_pointers>, int> == true,
                "");
  static_assert(cpp17::is_invocable_v<decltype(std::mem_fn(&member_pointers::pmf_add_one)),
                                      member_pointers, int> == true,
                "");

  static_assert(cpp17::is_invocable_v<decltype(&member_pointers::pmf_add_one), void*, int> == false,
                "");
  static_assert(
      cpp17::is_invocable_v<decltype(&member_pointers::pmf_add_one), std::nullptr_t, int> == false,
      "");
  static_assert(
      cpp17::is_invocable_v<decltype(&member_pointers::pmf_add_one), nothing, int> == false, "");
  static_assert(cpp17::is_invocable_v<decltype(&member_pointers::pmf_add_one), int> == false, "");

  static_assert(cpp17::is_invocable_v<decltype(&liar::pmf_add_one), liar, int> == true, "");
  static_assert(
      cpp17::is_invocable_v<decltype(&independent_liar::pmf_add_one), independent_liar, int> ==
          true,
      "");
  static_assert(cpp17::is_invocable_v<decltype(&member_pointers::pmf_add_one), liar, int> == true,
                "");
  static_assert(cpp17::is_invocable_v<decltype(&member_pointers::pmf_add_one), liar*, int> == true,
                "");
  static_assert(cpp17::is_invocable_v<decltype(&member_pointers::pmf_add_one), liar&, int> == true,
                "");
  static_assert(
      cpp17::is_invocable_v<decltype(&member_pointers::pmf_add_one), std::unique_ptr<liar>, int> ==
          true,
      "");

  static_assert(cpp17::is_invocable_v<decltype(&liar::pmf_add_one), member_pointers, int> == false,
                "");
  static_assert(
      cpp17::is_invocable_v<decltype(&member_pointers::pmf_add_one), independent_liar, int> ==
          false,
      "");
}

TEST(InvokeTraitsTest, PmdIsInvocable) {
  static_assert(
      cpp17::is_invocable_v<decltype(&member_pointers::pmd_add_one), member_pointers> == true, "");
  static_assert(
      cpp17::is_invocable_v<decltype(&member_pointers::pmd_add_one), member_pointers*> == true, "");
  static_assert(
      cpp17::is_invocable_v<decltype(&member_pointers::pmd_add_one), member_pointers&> == true, "");
  static_assert(
      cpp17::is_invocable_v<decltype(&member_pointers::pmd_add_one), member_pointers&&> == true,
      "");
  static_assert(cpp17::is_invocable_v<decltype(&member_pointers::pmd_add_one), liar> == true, "");
  static_assert(cpp17::is_invocable_v<decltype(&member_pointers::pmd_add_one),
                                      std::reference_wrapper<liar>> == true,
                "");
  static_assert(
      cpp17::is_invocable_v<decltype(&member_pointers::pmd_add_one), std::unique_ptr<liar>> == true,
      "");

  static_assert(
      cpp17::is_invocable_v<decltype(&member_pointers::pmd_add_one), independent_liar> == false,
      "");
}

TEST(InvokeTraitsTest, IsInvocableR) {
  static_assert(std::is_same_v<cpp17::invoke_result_t<decltype(func_add_one), int>, int> == true,
                "");
  static_assert(cpp17::is_invocable_r_v<void, decltype(noop)> == true, "");
  static_assert(cpp17::is_invocable_r_v<int, decltype(func_add_one), int> == true, "");
  static_assert(cpp17::is_invocable_r_v<long, decltype(func_add_one), int> == true, "");
  static_assert(cpp17::is_invocable_r_v<void, decltype(func_add_one), int> == true, "");
  // Narrowing is allowed in both directions
  static_assert(cpp17::is_invocable_r_v<char, decltype(func_add_one), long> == true, "");
  // int -> void* is a valid conversion, but not implicitly.
  static_assert(cpp17::is_invocable_r_v<void*, decltype(func_add_one), intptr_t> == false, "");
}

TEST(InvokeTraitsTest, InvokeResult) {
  // Wrap in lambda so we can decltype() (also no explicit template parameter).
  constexpr auto forward = [](auto&& t) -> decltype(auto) { return std::forward<decltype(t)>(t); };
  static_assert(std::is_same_v<cpp17::invoke_result_t<decltype(noop)>, void> == true, "");
  static_assert(std::is_same_v<cpp17::invoke_result_t<decltype(lambda_add_one), int>, int> == true,
                "");
  static_assert(std::is_same_v<cpp17::invoke_result_t<decltype(forward), int>, int&&> == true, "");
  static_assert(std::is_same_v<cpp17::invoke_result_t<decltype(forward), int&&>, int&&> == true,
                "");
  static_assert(std::is_same_v<cpp17::invoke_result_t<decltype(forward), int&>, int&> == true, "");

  static_assert(
      std::is_same_v<
          cpp17::invoke_result_t<decltype(&member_pointers::pmf_add_one), member_pointers, int>,
          int> == true,
      "");
  static_assert(
      std::is_same_v<
          cpp17::invoke_result_t<decltype(&member_pointers::pmf_add_one), member_pointers, short>,
          int> == true,
      "");
  static_assert(
      std::is_same_v<cpp17::invoke_result_t<decltype(&member_pointers::pmf_add_one), liar, int>,
                     int> == true,
      "");

  // Invoke PMD then invoke the result (it's a function pointer to func_add_one)
  static_assert(std::is_same_v<
                    cpp17::invoke_result_t<
                        cpp17::invoke_result_t<decltype(&member_pointers::pmd_add_one), liar>, int>,
                    int> == true,
                "");

  static_assert(std::is_same_v<cpp17::invoke_result_t<decltype(take_all)>, void> == true, "");
  static_assert(std::is_same_v<cpp17::invoke_result_t<decltype(take_all), int>, void> == true, "");
  static_assert(
      std::is_same_v<
          cpp17::invoke_result_t<decltype(take_all), void*, member_pointers, std::tuple<>>, void> ==
          true,
      "");

  constexpr auto forward_all = [](auto&&... ts) {
    return std::forward_as_tuple(std::forward<decltype(ts)>(ts)...);
  };
  static_assert(std::is_same_v<cpp17::invoke_result_t<decltype(forward_all), int, short, long>,
                               std::tuple<int&&, short&&, long&&>> == true,
                "");
  static_assert(std::is_same_v<cpp17::invoke_result_t<decltype(forward_all), int, short&, long&&>,
                               std::tuple<int&&, short&, long&&>> == true,
                "");
}

#if __cpp_lib_is_invocable >= 201703L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(InvokeTraitsTest, IsAliasForStd) {
  static_assert(std::is_same_v<cpp17::is_invocable<int>, std::is_invocable<int>>, "");
  static_assert(
      std::is_same_v<cpp17::is_invocable<void(int), int>, std::is_invocable<void(int), int>>, "");
  static_assert(std::is_same_v<cpp17::is_nothrow_invocable<int>, std::is_nothrow_invocable<int>>,
                "");
  static_assert(std::is_same_v<cpp17::is_nothrow_invocable<void(int), int>,
                               std::is_nothrow_invocable<void(int), int>>,
                "");

  static_assert(std::is_same_v<cpp17::is_invocable_r<int, int>, std::is_invocable_r<int, int>>, "");
  static_assert(std::is_same_v<cpp17::is_invocable_r<void, void(int), int>,
                               std::is_invocable_r<void, void(int), int>>,
                "");
  static_assert(std::is_same_v<cpp17::is_nothrow_invocable_r<int, int>,
                               std::is_nothrow_invocable_r<int, int>>,
                "");
  static_assert(std::is_same_v<cpp17::is_nothrow_invocable_r<void, void(int), int>,
                               std::is_nothrow_invocable_r<void, void(int), int>>,
                "");

  static_assert(
      std::is_same_v<cpp17::invoke_result<void(int), int>, std::invoke_result<void(int), int>>, "");
}

#endif  // __cpp_lib_is_invocable >= 201703L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

constexpr int TestIsConstantEvaluated(int x) {
  if (cpp20::is_constant_evaluated()) {
    return x + 2;
  }
  return x + 3;
}

TEST(ContextTraits, IsConstantEvaluated) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconstant-evaluated"
  constexpr decltype(auto) is = cpp20::is_constant_evaluated();
#pragma GCC diagnostic pop
  static_assert(cpp17::is_same_v<std::decay_t<decltype(is)>, bool>, "");

  decltype(auto) is_not = cpp20::is_constant_evaluated();
  static_assert(cpp17::is_same_v<decltype(is_not), bool>, "");
  EXPECT_FALSE(is_not);

  int x = 2;
  int y = TestIsConstantEvaluated(x);

#if defined(__cpp_lib_is_constant_evaluated) || \
    (defined(__has_builtin) && __has_builtin(__builtin_is_constant_evaluated))
  static_assert(is, "");
  constexpr int z = TestIsConstantEvaluated(2);
  static_assert(z == 4, "");
  EXPECT_EQ(5, y);
#else
  EXPECT_EQ(4, y);
#endif
}

#if defined(__cpp_lib_is_constant_evaluated) && __cpp_lib_is_constant_evaluated >= 201811L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)
TEST(ContextTraits, IsConstantEvaluatedIsAliasForStdWhenAvailable) {
  static_assert(&cpp20::is_constant_evaluated == &std::is_constant_evaluated, "");
}
#endif

}  // namespace
