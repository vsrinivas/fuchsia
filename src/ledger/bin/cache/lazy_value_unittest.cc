// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cache/lazy_value.h"

#include <lib/fit/function.h>

#include "gtest/gtest.h"
#include "src/ledger/lib/callback/capture.h"
#include "src/ledger/lib/callback/set_when_called.h"

namespace cache {
namespace {
TEST(LazyValueTest, SimpleGet) {
  auto generator = [](fit::function<void(size_t, size_t)> callback) { callback(0, 1); };

  LazyValue<size_t, size_t> cache(0, generator);

  bool called;
  size_t status;
  size_t value;
  cache.Get(ledger::Capture(ledger::SetWhenCalled(&called), &status, &value));
  ASSERT_TRUE(called);
  EXPECT_EQ(status, 0u);
  EXPECT_EQ(value, 1u);
}

TEST(LazyValueTest, FailingGenerator) {
  size_t nb_called = 0;
  auto generator = [&nb_called](fit::function<void(size_t, size_t)> callback) {
    ++nb_called;
    callback(1, 0);
  };

  LazyValue<size_t, size_t> cache(0, generator);

  bool called;
  size_t status;
  size_t value;

  cache.Get(ledger::Capture(ledger::SetWhenCalled(&called), &status, &value));
  ASSERT_TRUE(called);
  EXPECT_EQ(status, 1u);
  EXPECT_EQ(nb_called, 1u);

  cache.Get(ledger::Capture(ledger::SetWhenCalled(&called), &status, &value));
  ASSERT_TRUE(called);
  EXPECT_EQ(status, 1u);
  EXPECT_EQ(nb_called, 2u);
}

TEST(LazyValueTest, CacheCallback) {
  size_t nb_called = 0;
  fit::function<void(size_t, size_t)> generator_callback;
  auto generator = [&nb_called, &generator_callback](fit::function<void(size_t, size_t)> callback) {
    ++nb_called;
    generator_callback = std::move(callback);
  };

  LazyValue<size_t, size_t> cache(0, generator);

  bool called1, called2;
  size_t status1, status2;
  size_t value1, value2;

  cache.Get(ledger::Capture(ledger::SetWhenCalled(&called1), &status1, &value1));

  EXPECT_FALSE(called1);
  EXPECT_EQ(nb_called, 1u);

  cache.Get(ledger::Capture(ledger::SetWhenCalled(&called2), &status2, &value2));

  EXPECT_FALSE(called2);
  EXPECT_EQ(nb_called, 1u);

  generator_callback(0, 42);

  ASSERT_TRUE(called1);
  ASSERT_TRUE(called2);
  EXPECT_EQ(nb_called, 1u);
  EXPECT_EQ(status1, 0u);
  EXPECT_EQ(value1, 42u);
  EXPECT_EQ(status2, 0u);
  EXPECT_EQ(value2, 42u);
}

}  // namespace
}  // namespace cache
