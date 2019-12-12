// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/callback/ensure_called.h"

#include <tuple>
#include <utility>

#include "gtest/gtest.h"
#include "src/ledger/lib/callback/capture.h"
#include "src/ledger/lib/callback/set_when_called.h"

namespace ledger {
namespace {

TEST(EnsureCalled, NormalCall) {
  bool called;
  int called_with = 0;

  {
    auto callable = EnsureCalled(Capture(SetWhenCalled(&called), &called_with), 1);
    EXPECT_TRUE(callable);

    callable(2);
    EXPECT_TRUE(called);
    EXPECT_EQ(called_with, 2);

    EXPECT_FALSE(callable);
    called = false;
  }

  EXPECT_FALSE(called);
}

TEST(EnsureCalled, DestructorCall) {
  bool called;
  int called_with = 0;

  { auto call = EnsureCalled(Capture(SetWhenCalled(&called), &called_with), 1); }

  EXPECT_TRUE(called);
  EXPECT_EQ(called_with, 1);
}

TEST(EnsureCalled, MoveAssign) {
  bool called_internal = false;
  bool called_external = false;

  auto make_callback = [](bool* called) { return [called]() { *called = true; }; };
  auto callback_internal = make_callback(&called_internal);
  auto callback_external = make_callback(&called_external);

  auto external = EnsureCalled(std::move(callback_external));

  {
    auto internal = EnsureCalled(std::move(callback_internal));
    external = std::move(internal);
    EXPECT_FALSE(internal);
    EXPECT_TRUE(called_external);
  }
  EXPECT_FALSE(called_internal);

  external();
  EXPECT_TRUE(called_internal);
}

TEST(EnsureCalled, MoveConstruct) {
  bool called;

  {
    auto callback = EnsureCalled(SetWhenCalled(&called));

    {
      auto callback2 = std::move(callback);
      EXPECT_FALSE(callback);
      EXPECT_TRUE(callback2);
      EXPECT_FALSE(called);
    }
    EXPECT_TRUE(called);
    called = false;
  }
  EXPECT_FALSE(called);
}

TEST(EnsureCalled, EnsureCalledReturn) {
  auto callback = EnsureCalled([] { return true; });
  bool b = callback();
  EXPECT_TRUE(b);
}

}  // namespace
}  // namespace ledger
