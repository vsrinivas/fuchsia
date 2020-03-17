// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/camera/lib/hanging_get_helper/hanging_get_helper.h"

TEST(HangingGetHelperTest, SetReturnValues) {
  camera::HangingGetHelper<int> helper;
  EXPECT_FALSE(helper.Set(42));
  EXPECT_FALSE(helper.Set(42));
  EXPECT_TRUE(helper.Set(17));
}

TEST(HangingGetHelperTest, GetReturnValues) {
  camera::HangingGetHelper<int> helper;
  EXPECT_FALSE(helper.Get([](int) { ADD_FAILURE(); }));
  EXPECT_TRUE(helper.Get([](int) { ADD_FAILURE(); }));
}

TEST(HangingGetHelperTest, SimpleFlow) {
  camera::HangingGetHelper<int> helper;
  int returned = -1;
  EXPECT_FALSE(helper.Get([&](int x) { returned = x; }));
  EXPECT_FALSE(helper.Set(42));
  EXPECT_EQ(returned, 42);
  EXPECT_FALSE(helper.Set(17));
  EXPECT_FALSE(helper.Get([&](int x) { returned = x + 1; }));
  EXPECT_EQ(returned, 18);
  EXPECT_FALSE(helper.Get([&](int x) { returned = x + 2; }));
  EXPECT_TRUE(helper.Get([&](int x) { returned = x + 3; }));
  EXPECT_FALSE(helper.Set(37));
  EXPECT_EQ(returned, 40);
  EXPECT_FALSE(helper.Set(9));
  EXPECT_TRUE(helper.Set(70));
  EXPECT_FALSE(helper.Get([&](int x) { returned = x + 4; }));
  EXPECT_EQ(returned, 74);
}

TEST(HangingGetHelperTest, CustomTypeExplicitCompare) {
  struct Thing {
    int x;
  };
  auto eq = [](const Thing& a, const Thing& b) { return (a.x % 10) == (b.x % 10); };
  camera::HangingGetHelper<Thing, decltype(eq)> helper(eq);
  EXPECT_FALSE(helper.Set({42}));
  EXPECT_FALSE(helper.Set({52}));
  EXPECT_FALSE(helper.Set({82}));
  EXPECT_TRUE(helper.Set({17}));
  int returned = -1;
  EXPECT_FALSE(helper.Get([&](Thing t) { returned = t.x; }));
  EXPECT_EQ(returned, 17);
  EXPECT_FALSE(helper.Get([&](Thing t) { returned = t.x + 1; }));
  EXPECT_FALSE(helper.Set({27}));
  EXPECT_EQ(returned, 17);
}

TEST(HangingGetHelperTest, CustomTypeWithEqualityOperator) {
  struct Thing {
    int x;
    bool operator==(const Thing& other) const { return (other.x % 10) == (x % 10); }
  };
  camera::HangingGetHelper<Thing> helper;
  EXPECT_FALSE(helper.Set({42}));
  EXPECT_FALSE(helper.Set({52}));
  EXPECT_FALSE(helper.Set({82}));
  EXPECT_TRUE(helper.Set({17}));
  int returned = -1;
  EXPECT_FALSE(helper.Get([&](Thing t) { returned = t.x; }));
  EXPECT_EQ(returned, 17);
  EXPECT_FALSE(helper.Get([&](Thing t) { returned = t.x + 1; }));
  EXPECT_FALSE(helper.Set({27}));
  EXPECT_EQ(returned, 17);
}

struct GlobalThing {
  int x;
};

bool operator==(const GlobalThing& a, const GlobalThing& b) { return (a.x % 10) == (b.x % 10); }

TEST(HangingGetHelperTest, CustomTypeWithNonMemberEqualityOperator) {
  camera::HangingGetHelper<GlobalThing> helper;
  EXPECT_FALSE(helper.Set({42}));
  EXPECT_FALSE(helper.Set({52}));
  EXPECT_FALSE(helper.Set({82}));
  EXPECT_TRUE(helper.Set({17}));
  int returned = -1;
  EXPECT_FALSE(helper.Get([&](GlobalThing t) { returned = t.x; }));
  EXPECT_EQ(returned, 17);
  EXPECT_FALSE(helper.Get([&](GlobalThing t) { returned = t.x + 1; }));
  EXPECT_FALSE(helper.Set({27}));
  EXPECT_EQ(returned, 17);
}

TEST(HangingGetHelperTest, NonCopyableType) {
  struct Thing {
    int x;
    Thing(const Thing&) = delete;
    Thing(Thing&&) = default;
    Thing& operator=(const Thing&) = delete;
    Thing& operator=(Thing&&) = default;
  };
  camera::HangingGetHelper<Thing> helper;
  EXPECT_FALSE(helper.Set({42}));
  EXPECT_TRUE(helper.Set({42}));
  int returned = -1;
  EXPECT_FALSE(helper.Get([&](Thing t) { returned = t.x; }));
  EXPECT_EQ(returned, 42);
}
