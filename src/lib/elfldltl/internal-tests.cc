// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/internal/const-string.h>

#include <zxtest/zxtest.h>

namespace {

using elfldltl::internal::ConstString;

constexpr ConstString kEmpty("");
static_assert(kEmpty.empty());

constexpr ConstString kFoo = "foo";
static_assert(kFoo.size() == 3);
static_assert(std::string_view(kFoo) == "foo");
static_assert(kFoo.c_str()[3] == '\0');

constexpr ConstString kFoobar = kFoo + "bar";
static_assert(kFoobar.size() == 6);
static_assert(std::string_view(kFoobar) == "foobar");

constexpr ConstString kFoobarbaz = kFoo + "bar" + "baz";
static_assert(kFoobarbaz.size() == 9);
static_assert(std::string_view(kFoobarbaz) == "foobarbaz");

constexpr std::string_view kFooSv = kFoo;
static_assert(kFooSv == "foo");
static_assert(kFooSv.data() == kFoo.data());
static_assert(kFooSv.size() == kFoo.size());

TEST(ElfldltlInternalTests, ConstString) {
  EXPECT_STREQ(std::string_view(kFoo), "foo");
  EXPECT_STREQ(std::string_view(kFoobar), "foobar");
  EXPECT_STREQ(std::string_view(kFoobarbaz), "foobarbaz");
  EXPECT_STREQ(kFooSv, "foo");
}

}  // namespace
