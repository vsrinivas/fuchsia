// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/auto_cleanable.h"
#include "gtest/gtest.h"

namespace ledger {
namespace {

class Cleanable {
 public:
  void set_on_empty(const ftl::Closure& on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
  }

  void Clean() const {
    if (on_empty_callback_)
      on_empty_callback_();
  }

 private:
  ftl::Closure on_empty_callback_;
};

TEST(AutoCleanableSet, ClearsOnEmpty) {
  AutoCleanableSet<Cleanable> set;
  EXPECT_TRUE(set.empty());

  const auto& p1 = set.emplace().first;
  const auto& p2 = set.emplace().first;

  EXPECT_FALSE(set.empty());

  p1->Clean();
  EXPECT_FALSE(set.empty());

  p2->Clean();
  EXPECT_TRUE(set.empty());
}

TEST(AutoCleanableSet, CallsOnEmpty) {
  AutoCleanableSet<Cleanable> set;
  bool empty_called = false;
  set.set_on_empty([&empty_called] { empty_called = true; });

  EXPECT_FALSE(empty_called);

  const auto& p1 = set.emplace().first;
  EXPECT_FALSE(empty_called);

  p1->Clean();
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

}  // namespace
}  // namespace ledger
