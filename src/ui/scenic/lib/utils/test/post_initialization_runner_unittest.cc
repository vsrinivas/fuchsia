// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/utils/post_initialization_runner.h"

#include <memory>

#include <gtest/gtest.h>

namespace utils {
namespace test {

TEST(PostInitializationRunner, InitializeBefore) {
  PostInitializationRunner runner;
  int count = 0;

  runner.SetInitialized();
  runner.RunAfterInitialized([&]() { ++count; });
  runner.RunAfterInitialized([&]() { ++count; });
  EXPECT_EQ(count, 2);
}

TEST(PostInitializationRunner, InitializeAfter) {
  PostInitializationRunner runner;
  int count = 0;

  runner.RunAfterInitialized([&]() { ++count; });
  runner.RunAfterInitialized([&]() { ++count; });
  EXPECT_EQ(count, 0);
  runner.SetInitialized();
  EXPECT_EQ(count, 2);
}

TEST(PostInitializationRunner, InitializeBetween) {
  PostInitializationRunner runner;
  int count = 0;

  runner.RunAfterInitialized([&]() { ++count; });
  EXPECT_EQ(count, 0);
  runner.SetInitialized();
  EXPECT_EQ(count, 1);
  runner.RunAfterInitialized([&]() { ++count; });
  EXPECT_EQ(count, 2);
}

TEST(PostInitializationRunner, DestroyBeforeInitialize) {
  auto runner = std::make_unique<PostInitializationRunner>();
  int count = 0;

  runner->RunAfterInitialized([&]() { ++count; });
  runner->RunAfterInitialized([&]() { ++count; });
  runner.reset(0);
  EXPECT_EQ(count, 0);
}

}  // namespace test
}  // namespace utils
