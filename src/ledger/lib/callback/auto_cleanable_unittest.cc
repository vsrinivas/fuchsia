// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/callback/auto_cleanable.h"

#include <lib/async-testing/test_loop.h>
#include <lib/fit/function.h>
#include <zircon/compiler.h>

#include "gtest/gtest.h"

namespace ledger {
namespace {

class Cleanable {
 public:
  explicit Cleanable(int id = 0) : id(id) {}
  void SetOnDiscardable(fit::closure on_discardable) {
    on_discardable_ = std::move(on_discardable);
  }

  bool IsDiscardable() const { return cleaned; }

  void Clean() {
    cleaned = true;
    if (on_discardable_)
      on_discardable_();
  }

  int id;
  bool cleaned = false;

 private:
  fit::closure on_discardable_;
};

TEST(AutoCleanableSet, ClearsOnDiscardable) {
  async::TestLoop loop;

  AutoCleanableSet<Cleanable> set(loop.dispatcher());
  EXPECT_TRUE(set.empty());
  EXPECT_EQ(0UL, set.size());

  auto& p1 = set.emplace();
  auto& p2 = set.emplace();

  EXPECT_FALSE(set.empty());
  EXPECT_EQ(2UL, set.size());

  p1.Clean();

  loop.RunUntilIdle();
  EXPECT_FALSE(set.empty());
  EXPECT_EQ(1UL, set.size());

  p2.Clean();

  loop.RunUntilIdle();
  EXPECT_TRUE(set.empty());
  EXPECT_EQ(0UL, set.size());
}

TEST(AutoCleanableSet, Iterator) {
  async::TestLoop loop;

  AutoCleanableSet<Cleanable> set(loop.dispatcher());
  EXPECT_TRUE(set.empty());

  auto& p1 = set.emplace(1);
  auto& p2 = set.emplace(2);
  auto& p3 = set.emplace(3);
  auto& p4 = set.emplace(4);
  EXPECT_FALSE(set.empty());
  EXPECT_EQ(4UL, set.size());
  p2.Clean();
  loop.RunUntilIdle();

  AutoCleanableSet<Cleanable>::iterator it = set.begin();
  std::unordered_set<int> expected_ids{p1.id, p3.id, p4.id};

  // Test postfix increment
  std::unordered_set<int> actual_ids{it++->id, it++->id, it++->id};
  EXPECT_EQ(expected_ids, actual_ids);

  EXPECT_EQ(set.end(), it);

  it = set.begin();
  actual_ids.clear();

  // Test prefix increment
  actual_ids.insert(it->id);
  actual_ids.insert((++it)->id);
  actual_ids.insert((++it)->id);
  ++it;
  EXPECT_EQ(expected_ids, actual_ids);

  EXPECT_EQ(set.end(), it);
}

TEST(AutoCleanableSet, CallsOnDiscardable) {
  async::TestLoop loop;

  AutoCleanableSet<Cleanable> set(loop.dispatcher());
  bool discardable_called = false;
  set.SetOnDiscardable([&discardable_called] { discardable_called = true; });

  EXPECT_FALSE(discardable_called);

  auto& p1 = set.emplace();
  EXPECT_FALSE(discardable_called);

  p1.Clean();
  loop.RunUntilIdle();
  EXPECT_TRUE(discardable_called);
}

TEST(AutoCleanableMap, ClearsOnDiscardable) {
  async::TestLoop loop;

  AutoCleanableMap<int, Cleanable> map(loop.dispatcher());
  EXPECT_TRUE(map.empty());

  auto& p1 =
      map.emplace(std::piecewise_construct, std::forward_as_tuple(0), std::forward_as_tuple())
          .first->second;
  auto& p2 =
      map.emplace(std::piecewise_construct, std::forward_as_tuple(1), std::forward_as_tuple())
          .first->second;

  EXPECT_FALSE(map.empty());

  p1.Clean();
  loop.RunUntilIdle();
  EXPECT_FALSE(map.empty());

  p2.Clean();
  loop.RunUntilIdle();
  EXPECT_TRUE(map.empty());
}

TEST(AutoCleanableMap, CallsOnDiscardable) {
  async::TestLoop loop;

  AutoCleanableMap<int, Cleanable> map(loop.dispatcher());
  bool discardable_called = false;
  map.SetOnDiscardable([&discardable_called] { discardable_called = true; });

  EXPECT_FALSE(discardable_called);

  auto& p1 =
      map.emplace(std::piecewise_construct, std::forward_as_tuple(0), std::forward_as_tuple())
          .first->second;
  EXPECT_FALSE(discardable_called);

  p1.Clean();
  loop.RunUntilIdle();
  EXPECT_TRUE(discardable_called);
}

TEST(AutoCleanableMap, GetSize) {
  async::TestLoop loop;

  AutoCleanableMap<int, Cleanable> map(loop.dispatcher());

  EXPECT_EQ(0u, map.size());

  auto& p1 =
      map.emplace(std::piecewise_construct, std::forward_as_tuple(0), std::forward_as_tuple())
          .first->second;
  EXPECT_EQ(1u, map.size());

  auto& p2 =
      map.emplace(std::piecewise_construct, std::forward_as_tuple(1), std::forward_as_tuple())
          .first->second;

  auto& p3 =
      map.emplace(std::piecewise_construct, std::forward_as_tuple(2), std::forward_as_tuple())
          .first->second;

  EXPECT_EQ(3u, map.size());

  p1.Clean();
  p2.Clean();
  p3.Clean();
  loop.RunUntilIdle();
  EXPECT_EQ(0u, map.size());
}

TEST(AutoCleanableMap, GetBegin) {
  async::TestLoop loop;

  AutoCleanableMap<int, Cleanable> map(loop.dispatcher());

  const auto& p1 =
      map.emplace(std::piecewise_construct, std::forward_as_tuple(0), std::forward_as_tuple())
          .first;

  const auto& p2 =
      map.emplace(std::piecewise_construct, std::forward_as_tuple(1), std::forward_as_tuple())
          .first;

  const AutoCleanableMap<int, Cleanable>::iterator it1 = map.begin();

  EXPECT_EQ(it1, p1);

  EXPECT_NE(it1, p2);

  p1->second.Clean();
  loop.RunUntilIdle();

  EXPECT_EQ(map.begin(), p2);

  p2->second.Clean();
  loop.RunUntilIdle();

  EXPECT_EQ(map.begin(), map.end());
}

TEST(AutoCleanableMap, ConstIteration) {
  async::TestLoop loop;

  const AutoCleanableMap<int, Cleanable> map(loop.dispatcher());
  for (__UNUSED const auto& [key, value] : map) {
  }
}

TEST(AutoCleanableMap, Clear) {
  async::TestLoop loop;

  AutoCleanableMap<int, Cleanable> map(loop.dispatcher());
  map.emplace(std::piecewise_construct, std::forward_as_tuple(0), std::forward_as_tuple());
  map.emplace(std::piecewise_construct, std::forward_as_tuple(1), std::forward_as_tuple());
  map.emplace(std::piecewise_construct, std::forward_as_tuple(2), std::forward_as_tuple());

  EXPECT_FALSE(map.empty());

  map.clear();

  EXPECT_TRUE(map.empty());
}

}  // namespace
}  // namespace ledger
