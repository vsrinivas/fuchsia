// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/version.h>

#include <gtest/gtest.h>

namespace {

#if __cplusplus > 201703L

TEST(VersionTest, FeatureTestMacrosForCpp20) {
#if __cplusplus >= 201803L
  // TODO(fxb/67616): This is not necessarily true with the state of C++20 implementations now.
  // static_assert(__cpp_lib_string_view == 201803L);
#else
  static_assert(__cpp_lib_string_view == 201606L);
#endif

  static_assert(__cpp_lib_logical_traits == 201510L);
  static_assert(__cpp_lib_void_t == 201411L);
  static_assert(__cpp_lib_optional == 201606L,
                "'__cpp_optional_lib' should be using draft 201606L in std20.");
}

#elif __cplusplus > 201402L

TEST(VersionTest, FeatureTestMacrosForCpp17) {
#if __cplusplus >= 201606L
  static_assert(__cpp_lib_string_view == 201606L);
#endif
#if __cplusplus >= 201510L
  static_assert(__cpp_lib_logical_traits == 201510L);
#endif
#if __cplusplus >= 201411L
  static_assert(__cpp_lib_void_t == 201411L);
#endif
#if __cplusplus >= 201606L
  static_assert(__cpp_lib_optional == 201606L,
                "'__cpp_lib_optional' should be using draft 201606L in std17.");
  static_assert(__cpp_lib_variant == 201606L,
                "'__cpp_lib_variant' should be using draft 201606L in std17.");
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
  static_assert(false, "'__cpp_lib_optional' should not be defined in std14.")
#endif
#if defined(__cpp_lib_variant)
      static_assert(false, "'__cpp_lib_variant' should not be defined in std14.")
#endif
}
#endif

}  // namespace
