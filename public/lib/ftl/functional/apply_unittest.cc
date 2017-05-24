// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/functional/apply.h"

#include "gtest/gtest.h"

#include <memory>

namespace ftl {
namespace {

TEST(Apply, SimpleCall) {
  EXPECT_EQ(3,
            Apply([](int i, int j) { return i + j; }, std::make_tuple(1, 2)));
}

TEST(Apply, NoReturnValue) {
  int result;
  Apply([&result](int i, int j) { result = i + j; }, std::make_tuple(1, 2));
  EXPECT_EQ(3, result);
}

TEST(Apply, Moveable) {
  EXPECT_EQ(3, Apply([](std::unique_ptr<int> i, int j) { return *i + j; },
                     std::make_tuple(std::make_unique<int>(1), 2)));
}

}  // namespace
}  // namespace ftl
