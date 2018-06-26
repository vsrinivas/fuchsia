// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/callback/auto_cleanable.h"

#include <lib/fit/function.h>
#include "gtest/gtest.h"

namespace callback {
namespace {

class Cleanable {
 public:
  explicit Cleanable(int id = 0) : id(id) {}
  void set_on_empty(fit::closure on_empty_callback) {
    on_empty_callback_ = std::move(on_empty_callback);
  }

  void Clean() const {
    if (on_empty_callback_)
      on_empty_callback_();
  }

  int id;

 private:
  fit::closure on_empty_callback_;
};

TEST(AutoCleanableSet, ClearsOnEmpty) {
  AutoCleanableSet<Cleanable> set;
  EXPECT_TRUE(set.empty());

  auto& p1 = set.emplace();
  auto& p2 = set.emplace();

  EXPECT_FALSE(set.empty());

  p1.Clean();
  EXPECT_FALSE(set.empty());

  p2.Clean();
  EXPECT_TRUE(set.empty());
}

TEST(AutoCleanableSet, Iterator) {
  AutoCleanableSet<Cleanable> set;
  EXPECT_TRUE(set.empty());

  auto& p1 = set.emplace(1);
  auto& p2 = set.emplace(2);
  auto& p3 = set.emplace(3);
  auto& p4 = set.emplace(4);
  EXPECT_FALSE(set.empty());
  p2.Clean();

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

TEST(AutoCleanableSet, CallsOnEmpty) {
  AutoCleanableSet<Cleanable> set;
  bool empty_called = false;
  set.set_on_empty([&empty_called] { empty_called = true; });

  EXPECT_FALSE(empty_called);

  auto& p1 = set.emplace();
  EXPECT_FALSE(empty_called);

  p1.Clean();
  EXPECT_TRUE(empty_called);
}

TEST(AutoCleanableMap, ClearsOnEmpty) {
  AutoCleanableMap<int, Cleanable> map;
  EXPECT_TRUE(map.empty());

  auto& p1 = map.emplace(std::piecewise_construct, std::forward_as_tuple(0),
                         std::forward_as_tuple())
                 .first->second;
  auto& p2 = map.emplace(std::piecewise_construct, std::forward_as_tuple(1),
                         std::forward_as_tuple())
                 .first->second;

  EXPECT_FALSE(map.empty());

  p1.Clean();
  EXPECT_FALSE(map.empty());

  p2.Clean();
  EXPECT_TRUE(map.empty());
}

TEST(AutoCleanableMap, CallsOnEmpty) {
  AutoCleanableMap<int, Cleanable> map;
  bool empty_called = false;
  map.set_on_empty([&empty_called] { empty_called = true; });

  EXPECT_FALSE(empty_called);

  auto& p1 = map.emplace(std::piecewise_construct, std::forward_as_tuple(0),
                         std::forward_as_tuple())
                 .first->second;
  EXPECT_FALSE(empty_called);

  p1.Clean();
  EXPECT_TRUE(empty_called);
}

TEST(AutoCleanableMap, GetSize) {
  AutoCleanableMap<int, Cleanable> map;

  EXPECT_EQ(0u, map.size());

  auto& p1 = map.emplace(std::piecewise_construct, std::forward_as_tuple(0),
                         std::forward_as_tuple())
                 .first->second;
  EXPECT_EQ(1u, map.size());

  auto& p2 = map.emplace(std::piecewise_construct, std::forward_as_tuple(1),
                         std::forward_as_tuple())
                 .first->second;

  auto& p3 = map.emplace(std::piecewise_construct, std::forward_as_tuple(2),
                         std::forward_as_tuple())
                 .first->second;

  EXPECT_EQ(3u, map.size());

  p1.Clean();
  p2.Clean();
  p3.Clean();
  EXPECT_EQ(0u, map.size());
}

TEST(AutoCleanableMap, GetBegin) {
  AutoCleanableMap<int, Cleanable> map;

  const auto& p1 =
      map.emplace(std::piecewise_construct, std::forward_as_tuple(0),
                  std::forward_as_tuple())
          .first;

  const auto& p2 =
      map.emplace(std::piecewise_construct, std::forward_as_tuple(1),
                  std::forward_as_tuple())
          .first;

  const AutoCleanableMap<int, Cleanable>::iterator it1 = map.begin();

  EXPECT_EQ(it1, p1);

  EXPECT_NE(it1, p2);

  p1->second.Clean();

  EXPECT_EQ(map.begin(), p2);

  p2->second.Clean();

  EXPECT_EQ(map.begin(), map.end());
}

}  // namespace
}  // namespace callback
