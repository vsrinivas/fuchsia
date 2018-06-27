// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cache/lru_cache.h"

#include <lib/fit/function.h>

#include "gtest/gtest.h"
#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"

namespace cache {
namespace {
TEST(LRUCacheTest, SimpleGet) {
  auto generator = [](size_t i, fit::function<void(size_t, size_t)> callback) {
    callback(0, 2 * i);
  };

  LRUCache<size_t, size_t, size_t> cache(200, 0, generator);

  bool called;
  size_t status;
  size_t value;
  cache.Get(
      0, callback::Capture(callback::SetWhenCalled(&called), &status, &value));
  ASSERT_TRUE(called);
  EXPECT_EQ(0u, status);
  EXPECT_EQ(0u, value);

  cache.Get(
      42, callback::Capture(callback::SetWhenCalled(&called), &status, &value));
  ASSERT_TRUE(called);
  EXPECT_EQ(0u, status);
  EXPECT_EQ(84u, value);
}

TEST(LRUCacheTest, FailingGenerator) {
  size_t nb_called = 0;
  auto generator = [&nb_called](size_t i,
                                fit::function<void(size_t, size_t)> callback) {
    ++nb_called;
    callback(1, 0);
  };

  LRUCache<size_t, size_t, size_t> cache(200, 0, generator);

  bool called;
  size_t status;
  size_t value;

  cache.Get(
      0, callback::Capture(callback::SetWhenCalled(&called), &status, &value));
  ASSERT_TRUE(called);
  EXPECT_EQ(1u, status);
  EXPECT_EQ(1u, nb_called);

  cache.Get(
      0, callback::Capture(callback::SetWhenCalled(&called), &status, &value));
  ASSERT_TRUE(called);
  EXPECT_EQ(1u, status);
  EXPECT_EQ(2u, nb_called);
}

TEST(LRUCacheTest, CacheCallback) {
  size_t nb_called = 0;
  fit::function<void(size_t, size_t)> generator_callback;
  auto generator = [&nb_called, &generator_callback](
                       size_t i, fit::function<void(size_t, size_t)> callback) {
    ++nb_called;
    generator_callback = std::move(callback);
  };

  LRUCache<size_t, size_t, size_t> cache(200, 0, generator);

  bool called1, called2;
  size_t status1, status2;
  size_t value1, value2;

  cache.Get(0, callback::Capture(callback::SetWhenCalled(&called1), &status1,
                                 &value1));

  EXPECT_FALSE(called1);
  EXPECT_EQ(1u, nb_called);

  cache.Get(0, callback::Capture(callback::SetWhenCalled(&called2), &status2,
                                 &value2));

  EXPECT_FALSE(called2);
  EXPECT_EQ(1u, nb_called);

  generator_callback(0, 42);

  ASSERT_TRUE(called1);
  ASSERT_TRUE(called2);
  EXPECT_EQ(1u, nb_called);
  EXPECT_EQ(0u, status1);
  EXPECT_EQ(42u, value1);
  EXPECT_EQ(0u, status2);
  EXPECT_EQ(42u, value2);
}

TEST(LRUCacheTest, LRUPolicy) {
  size_t nb_called = 0;
  fit::function<void(size_t, size_t)> generator_callback;
  auto generator = [&nb_called](size_t i,
                                fit::function<void(size_t, size_t)> callback) {
    ++nb_called;
    callback(0u, 0u);
  };

  LRUCache<size_t, size_t, size_t> cache(3, 0, generator);

  size_t status;
  size_t value;

  cache.Get(0, callback::Capture([] {}, &status, &value));
  EXPECT_EQ(1u, nb_called);
  cache.Get(0, callback::Capture([] {}, &status, &value));
  EXPECT_EQ(1u, nb_called);
  cache.Get(1, callback::Capture([] {}, &status, &value));
  EXPECT_EQ(2u, nb_called);
  cache.Get(2, callback::Capture([] {}, &status, &value));
  EXPECT_EQ(3u, nb_called);
  cache.Get(0, callback::Capture([] {}, &status, &value));
  EXPECT_EQ(3u, nb_called);
  cache.Get(1, callback::Capture([] {}, &status, &value));
  EXPECT_EQ(3u, nb_called);
  cache.Get(2, callback::Capture([] {}, &status, &value));
  EXPECT_EQ(3u, nb_called);
  cache.Get(3, callback::Capture([] {}, &status, &value));
  EXPECT_EQ(4u, nb_called);
  cache.Get(1, callback::Capture([] {}, &status, &value));
  EXPECT_EQ(4u, nb_called);
  cache.Get(2, callback::Capture([] {}, &status, &value));
  EXPECT_EQ(4u, nb_called);
  cache.Get(3, callback::Capture([] {}, &status, &value));
  EXPECT_EQ(4u, nb_called);
  cache.Get(0, callback::Capture([] {}, &status, &value));
  EXPECT_EQ(5u, nb_called);
}

}  // namespace
}  // namespace cache
