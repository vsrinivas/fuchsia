// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/callback/destruction_guard.h"

#include "gtest/gtest.h"
#include "lib/ftl/functional/closure.h"

namespace callback {
namespace {

auto SetOnCall(bool* called) {
  return [called] { *called = true; };
}

TEST(DestructionGuard, OnDestruction) {
  bool called = false;
  {
    auto guard = MakeDestructionGuard(SetOnCall(&called));
    EXPECT_FALSE(called);
  }
  EXPECT_TRUE(called);
}

TEST(DestructionGuard, Reset) {
  bool called = false;
  {
    auto guard = MakeDestructionGuard(SetOnCall(&called));
    guard.Reset();
    guard.Reset(nullptr);
    EXPECT_FALSE(called);
  }
  EXPECT_FALSE(called);
}

TEST(DestructionGuard, ResetWithValue) {
  bool called1 = false, called2 = false;
  {
    DestructionGuard<ftl::Closure> guard(SetOnCall(&called1));
    guard.Reset(SetOnCall(&called2));
    EXPECT_FALSE(called1);
    EXPECT_FALSE(called2);
  }
  EXPECT_FALSE(called1);
  EXPECT_TRUE(called2);
}

TEST(DestructionGuard, MoveConstructor) {
  bool called = false;
  auto guard = MakeDestructionGuard(SetOnCall(&called));
  {
    auto guard2(std::move(guard));
    EXPECT_FALSE(called);
  }
  EXPECT_TRUE(called);
}

TEST(DestructionGuard, MoveOperator) {
  bool called = false;
  DestructionGuard<ftl::Closure> guard(SetOnCall(&called));
  {
    DestructionGuard<ftl::Closure> guard2;
    {
      guard2 = std::move(guard);
      EXPECT_FALSE(called);
    }
    EXPECT_FALSE(called);
  }
  EXPECT_TRUE(called);
}

}  // namespace
}  // namespace callback
