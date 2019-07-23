// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/fuzzing/traits.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fidl/cpp/vector.h>

#include <array>
#include <memory>
#include <string>
#include <utility>

#include "gtest/gtest.h"

namespace {

constexpr size_t size0 = 0;

TEST(TraitsTest, EmptyMinSizesMatch) {
  EXPECT_EQ(::fuzzing::MinSize<::fidl::StringPtr>(), size0);
  EXPECT_EQ(::fuzzing::MinSize<::fidl::VectorPtr<bool>>(), size0);
}

}  // namespace
