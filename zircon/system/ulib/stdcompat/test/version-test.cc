// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/version.h>

#include <version>

#include <gtest/gtest.h>

namespace {

#if __cplusplus > 201703L

TEST(StdCompatTest, FeatureTestMacrosForCpp20) {
#if __cplusplus >= 201803L
  static_assert(__cpp_lib_string_view == 201803L);
#else
  static_assert(__cpp_lib_string_view == 201606L);
#endif
}

#elif __cplusplus > 201402L

TEST(StdCompatTest, FeatureTestMacrosForCpp17) {
#if __cplusplus >= 201606L
  static_assert(__cpp_lib_string_view == 201606L);
#endif
}

#elif __cplusplus > 201103L

TEST(StdCompatTest, FeatureTestMacrosForCpp14) {
#if defined(__cpp_lib_string_view)
  static_assert(false, "'__cpp_lib_string_view' should not be defined in std14.")
#endif
}
#endif

}  // namespace
