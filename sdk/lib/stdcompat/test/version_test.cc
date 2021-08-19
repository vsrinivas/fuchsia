// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/version.h>

#include <gtest/gtest.h>

namespace {

#if __cplusplus > 201703L

TEST(VersionTest, FeatureTestMacrosForCpp20) {
  // TODO(fxb/67616): When std20 implementation catches up, string view implementation should be at
  // latest draft.
#if __cplusplus >= 201803L && __cpp_lib_string_view > 201606L
  static_assert(__cpp_lib_string_view == 201803L,
                "'__cpp_lib_string_view' should be using draft 201803L for c++20.");
#else
  static_assert(__cpp_lib_string_view == 201606L,
                "'__cpp_lib_string_view' should be using draft 201606L for c++20.");
#endif

  static_assert(__cpp_lib_variant == 202102L,
                "'__cpp_lib_variant' should be using draft 202102L in c++20.");

  static_assert(__cpp_lib_byte == 201603L,
                "'__cpp_lib_byte' should be using draft 201603L in c++20.");
  static_assert(__cpp_lib_logical_traits == 201510L,
                "'__cpp_lib_logical_traits' should be using draft 201510L.");
  static_assert(__cpp_lib_void_t == 201411L,
                "'__cpp_lib_void_t' should be using draft 201411L for c++20.");
  static_assert(__cpp_lib_optional == 201606L,
                "'__cpp_optional_lib' should be using draft 201606L in c++20.");
  static_assert(__cpp_lib_addressof_constexpr == 201603L,
                "'__cpp_lib_addressof_constexpr' should be using draft 201603L for c++20.");
  static_assert(__cpp_lib_nonmember_container_access == 201411L,
                "'__cpp_lib_nonmember_container_access' should be using draft 201411L in c++20.");
  static_assert(__cpp_lib_bounded_array_traits == 201902L,
                "'__cpp_lib_bounded_array_traits' should be using draft 201902L for c++20.");
  static_assert(__cpp_lib_remove_cvref == 201711L,
                "'__cpp_lib_remove_cvref' should be using draft 201711L for c++20.");
  static_assert(__cpp_lib_as_const == 201510L,
                "'__cpp_lib_as_const' should be using draft 201510L for c++17.");
#if defined(__cpp_lib_type_identity)
  static_assert(__cpp_lib_type_identity == 201806L,
                "'__cpp_lib_type_identity' should be using draft 201806L for c++20.");
#endif
  // TODO(fxb/67616): When libc++'s __cplusplus reflects C++20 as specified in the standard, move
  // this into a separate #if branch for C++23.
#if defined(__cpp_lib_is_scoped_enum)
  static_assert(__cpp_lib_is_scoped_enum == 202011L,
                "'__cpp_lib_is_scoped_enum' should be using draft 202011L for c++20.");
#endif
#if defined(__cpp_lib_is_constant_evaluated)
  static_assert(__cpp_lib_is_constant_evaluated == 201811L,
                "'__cpp_lib_is_constant_evaluated' should be using draft 201811L for c++20.");
#endif
}

#elif __cplusplus > 201402L

TEST(VersionTest, FeatureTestMacrosForCpp17) {
#if __cplusplus >= 201703L
  static_assert(__cpp_lib_is_aggregate == 201703L,
                "'__cpp_lib_is_aggregate' should be using draft 201703L for c++17.");
  static_assert(__cpp_lib_is_invocable == 201703L,
                "'__cpp_lib_is_invocable' should be using draft 201703L in c++17.");
#endif
#if __cplusplus >= 201606L
  static_assert(__cpp_lib_string_view == 201606L,
                "'__cpp_lib_string_view' should be using draft for c++17.");
  static_assert(__cpp_lib_optional == 201606L,
                "'__cpp_lib_optional' should be using draft 201606L in c++17.");
  static_assert(__cpp_lib_variant >= 201606L,
                "'__cpp_lib_variant' should be using at least draft 201606L in c++17.");
#endif
#if __cplusplus >= 201603L
  static_assert(__cpp_lib_addressof_constexpr == 201603L,
                "'__cpp_lib_addressof_constexpr' should be using draft 201603L for c++17.");
  static_assert(__cpp_lib_byte == 201603L,
                "'__cpp_lib_byte' should be using draft 201603L in c++17.");
  static_assert(__cpp_lib_apply == 201603L,
                "'__cpp_lib_apply' should be using draft 201603L in c++17.");
#endif
#if __cplusplus >= 201510L
  static_assert(__cpp_lib_logical_traits == 201510L,
                "'__cpp_lib_logical_traits' should be using draft 201510L for c++17.");
  static_assert(
      __cpp_lib_type_trait_variable_templates == 201510L,
      "'__cpp_lib_type_trait_variable_templates' should be using draft 201510L for c++17.");
  static_assert(__cpp_lib_as_const == 201510L,
                "'__cpp_lib_as_const' should be using draft 201510L for c++17.");
#endif
#if __cplusplus >= 201505L
  static_assert(__cpp_lib_bool_constant == 201505L,
                "'__cpp_lib_bool_constant' should be using draft 201505L in c++17.");
#endif
#if __cplusplus >= 201411L
  static_assert(__cpp_lib_void_t == 201411L,
                "'__cpp_lib_void_t' should be using draft 201411L in c++17.");
  static_assert(__cpp_lib_nonmember_container_access == 201411L,
                "'__cpp_lib_nonmember_container_access' should be using draft 201411L in c++17.");
  static_assert(__cpp_lib_invoke == 201411L,
                "'__cpp_lib_invoke' should be using draft 201411L in c++17.");
#endif
#if defined(__cpp_lib_is_constant_evaluated)
  static_assert(false, "'__cpp_lib_is_constant_evaluated' should not be defined in c++17.");
#endif
}

#elif __cplusplus > 201103L

TEST(VersionTest, FeatureTestMacrosForCpp14) {
#if defined(__cpp_lib_string_view)
  static_assert(false, "'__cpp_lib_string_view' should not be defined in c++14.");
#endif
#if defined(__cpp_lib_logical_traits)
  static_assert(false, "'__cpp_lib_logical_traits' should not be defined in c++14.");
#endif
#if defined(__cpp_lib_void_t)
  static_assert(false, "'__cpp_lib_void_t' should not be defined in c++14.");
#endif
#if defined(__cpp_lib_optional)
  static_assert(false, "'__cpp_lib_optional' should not be defined in c++14.")
#endif
#if defined(__cpp_lib_variant)
      static_assert(false, "'__cpp_lib_variant' should not be defined in c++14.")
#endif
#if defined(__cpp_lib_addressof_constexpr)
          static_assert(false, "'__cpp_lib_addressof_constexpr' should not be defined for c++14.")
#endif
#if defined(__cpp_lib_nonmember_container_access)
              static_assert(
                  false, "'__cpp_lib_nonmember_container_access' should not be defined for c++14.")
#endif
#if defined(__cpp_lib_bool_constant)
                  static_assert(false, "'__cpp_lib_bool_constant' should not be defined in c++14.");
#endif
#if defined(__cpp_lib_type_trait_variable_templates)
  static_assert(false, "'__cpp_lib_type_trait_variable_templates' should not be defined in c++14.");
#endif
#if defined(__cpp_lib_is_aggregate)
  static_assert(false, "'__cpp_lib_is_aggregate' should not be defined in c++14.");
#endif
#if defined(__cpp_lib_is_invocable)
  static_assert(false, "'__cpp_lib_is_invocable' should not be defined in c++14.");
#endif
#if defined(__cpp_lib_invoke)
  static_assert(false, "'__cpp_lib_is_invoke' should not be defined in c++14.");
#endif
#if defined(__cpp_lib_apply)
  static_assert(false, "'__cpp_lib_apply' should not be defined in c++14.");
#endif
#if defined(__cpp_lib_as_const)
  static_assert(false, "'__cpp_lib_as_const' should not be defined in c++14.");
#endif
#if defined(__cpp_lib_is_constant_evaluated)
  static_assert(false, "'__cpp_lib_is_constant_evaluated ' should not be defined in c++14.");
#endif
}

#endif

}  // namespace
