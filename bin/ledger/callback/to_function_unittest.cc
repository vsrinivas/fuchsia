// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/callback/to_function.h"

#include <memory>

#include "gtest/gtest.h"

namespace callback {
namespace {

TEST(ToStdFunction, SimpleConversion) {
  bool called = false;
  auto function = ToStdFunction([&] { called = true; });
  function();
  EXPECT_TRUE(called);
}

TEST(ToStdFunction, NotCopyableLambda) {
  bool called = false;
  auto ptr_to_called = std::make_unique<bool*>(&called);
  auto function =
      ToStdFunction([ptr = std::move(ptr_to_called)] { **ptr = true; });
  function();
  EXPECT_TRUE(called);
}

}  // namespace
}  // namespace callback
