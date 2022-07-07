// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/tokens/tokens.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <vector>

#include <gtest/gtest.h>

namespace camera {
namespace {

TEST(Tokens, Basic) {
  int count = 0;
  {
    std::unique_ptr<SharedToken<int>> ptr;
    {
      SharedToken<int> token(42, [&] { ++count; });
      EXPECT_EQ(count, 0);
      {
        std::vector<SharedToken<int>> copies(10, token);
        EXPECT_EQ(count, 0);
      }
      EXPECT_EQ(count, 0);
      ptr = std::make_unique<SharedToken<int>>(token);
    }
    EXPECT_EQ(count, 0);
    std::optional<SharedToken<int>> opt = *ptr;
    EXPECT_EQ(count, 0);
    ptr = nullptr;
    EXPECT_EQ(count, 0);
    opt = std::nullopt;
    EXPECT_EQ(count, 1);
  }
  EXPECT_EQ(count, 1);
}

TEST(Tokens, Dispatchers) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Loop other(&kAsyncLoopConfigNoAttachToCurrentThread);
  int count = 0;
  {
    SharedToken<int> token(
        42, [&] { ++count; }, loop.dispatcher());
  }
  EXPECT_EQ(count, 0);
  other.RunUntilIdle();
  EXPECT_EQ(count, 0);
  loop.RunUntilIdle();
  EXPECT_EQ(count, 1);
  {
    SharedToken<int> token(
        42, [&] { ++count; }, nullptr);
  }
  EXPECT_EQ(count, 1);
  other.RunUntilIdle();
  EXPECT_EQ(count, 1);
  loop.RunUntilIdle();
  EXPECT_EQ(count, 2);
  {
    SharedToken<int> token(
        42, [&] { ++count; }, other.dispatcher());
  }
  EXPECT_EQ(count, 2);
  loop.RunUntilIdle();
  EXPECT_EQ(count, 2);
  other.RunUntilIdle();
  EXPECT_EQ(count, 3);
}

TEST(Tokens, WithObject) {
  int count = 0;
  {
    std::vector<int> vec{42};
    SharedToken<std::vector<int>> token(std::move(vec), [&] { ++count; });
    EXPECT_EQ(count, 0);
    EXPECT_EQ((*token)[0], 42);
    auto other = token;
    EXPECT_EQ(count, 0);
    EXPECT_EQ((*other)[0], 42);
    (*other)[0] = 5;
    EXPECT_EQ((*token)[0], 5);
  }
  EXPECT_EQ(count, 1);
}

}  // namespace
}  // namespace camera
