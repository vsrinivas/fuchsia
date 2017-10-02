// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/callback/ensure_copyable.h"

#include <type_traits>

#include "gtest/gtest.h"

namespace callback {
namespace {

TEST(ToStdFunction, SimpleConversion) {
  bool called = false;
  auto lambda = [&]() { called = true; };
  auto copyable = EnsureCopyable(std::move(lambda));
  auto is_same = std::is_same<decltype(lambda), decltype(copyable)>::value;
  EXPECT_TRUE(is_same);
  auto copy = copyable;
  copy();
  EXPECT_TRUE(called);
}

TEST(ToStdFunction, NotCopyableLambda) {
  bool called = false;
  auto ptr_to_called = std::make_unique<bool*>(&called);
  auto lambda = [ptr = std::move(ptr_to_called)] { **ptr = true; };
  auto copyable = EnsureCopyable(std::move(lambda));
  auto is_same = std::is_same<decltype(lambda), decltype(copyable)>::value;
  EXPECT_FALSE(is_same);
  auto copy = copyable;
  copy();
  EXPECT_TRUE(called);
}

}  // namespace
}  // namespace callback
