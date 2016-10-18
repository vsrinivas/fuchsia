// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxtl/type_support.h>

// match_cv tests:
static_assert(mxtl::is_same<mxtl::match_cv<int, void>::type, void>::value, "wrong type");
static_assert(mxtl::is_same<mxtl::match_cv<const int, void>::type, const void>::value,
              "wrong type");
static_assert(mxtl::is_same<mxtl::match_cv<volatile void, char>::type, volatile char>::value,
              "wrong type");
static_assert(mxtl::is_same<mxtl::match_cv<const int, const char>::type, const char>::value,
              "wrong type");
static_assert(mxtl::is_same<mxtl::match_cv<const int, volatile char>::type, const char>::value,
              "wrong type");
static_assert(mxtl::is_same<mxtl::match_cv<char, const volatile void>::type, void>::value,
              "wrong type");
