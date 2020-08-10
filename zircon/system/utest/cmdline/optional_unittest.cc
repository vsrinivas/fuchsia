// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cmdline/optional.h>
#include <string.h>

#include <zxtest/zxtest.h>

namespace cmdline {
namespace {

TEST(Optional, Equality) {
  ASSERT_TRUE(Optional<int32_t>(1) == Optional<int32_t>(1));
  ASSERT_TRUE(Optional<int32_t>() == Optional<int32_t>());
  ASSERT_TRUE(Optional<std::string>("hello") == Optional<std::string>("hello"));

  ASSERT_FALSE(Optional<int32_t>(1) == Optional<int32_t>(2));
  ASSERT_FALSE(Optional<int32_t>() == Optional<int32_t>(0));
  ASSERT_FALSE(Optional<std::string>("hello") == Optional<std::string>("world"));
}

TEST(Optional, Inequality) {
  ASSERT_TRUE(Optional<int32_t>(1) != Optional<int32_t>(2));
  ASSERT_TRUE(Optional<int32_t>() != Optional<int32_t>(0));
  ASSERT_TRUE(Optional<std::string>("hello") != Optional<std::string>("world"));

  ASSERT_FALSE(Optional<int32_t>(1) != Optional<int32_t>(1));
  ASSERT_FALSE(Optional<int32_t>() != Optional<int32_t>());
  ASSERT_FALSE(Optional<std::string>("hello") != Optional<std::string>("hello"));
}

}  // namespace
}  // namespace cmdline
