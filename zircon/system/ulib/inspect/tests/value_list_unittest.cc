// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <lib/inspect/cpp/inspect.h>

#include <type_traits>

#include <zxtest/zxtest.h>

#include "lib/inspect/cpp/hierarchy.h"
#include "lib/inspect/cpp/reader.h"

using inspect::Inspector;
using inspect::IntPropertyValue;
using inspect::Node;
using inspect::ValueList;

namespace {

struct TestStruct {
  fit::deferred_action<fit::closure> cb;
};

TEST(ValueList, Basic) {
  Inspector inspector;
  ValueList list;
  list.emplace(inspector.GetRoot().CreateChild("abcd"));
  inspector.GetRoot().CreateInt("int", 22, &list);

  auto result = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  ASSERT_EQ(1u, hierarchy.children().size());
  EXPECT_EQ("abcd", hierarchy.children()[0].name());
  ASSERT_EQ(1u, hierarchy.node().properties().size());
  EXPECT_EQ("int", hierarchy.node().properties()[0].name());
  EXPECT_EQ(22, hierarchy.node().properties()[0].Get<IntPropertyValue>().value());
}

TEST(ValueList, Struct) {
  Inspector inspector;
  bool called = false;
  {
    ValueList list;
    list.emplace(TestStruct{.cb = fit::defer<fit::closure>([&] { called = true; })});
    EXPECT_FALSE(called);
  }
  EXPECT_TRUE(called);
}

TEST(ValueList, Types) {
  Inspector inspector;
  auto& root = inspector.GetRoot();
  ValueList list;

  root.CreateChild("child", &list);
  root.CreateInt("int", 0, &list);
  root.CreateUint("uint", 0, &list);
  root.CreateDouble("double", 0, &list);
  root.CreateString("string", "test", &list);
  root.CreateByteVector("bytes", std::vector<uint8_t>({0, 1, 2}), &list);
  {
    auto val = root.CreateIntArray("int array", 10);
    list.emplace(std::move(val));
  }
  {
    auto val = root.CreateUintArray("uint array", 10);
    list.emplace(std::move(val));
  }
  {
    auto val = root.CreateDoubleArray("double array", 10);
    list.emplace(std::move(val));
  }
  {
    auto val = root.CreateLinearIntHistogram("linear int", 0, 1, 10);
    list.emplace(std::move(val));
  }
  {
    auto val = root.CreateLinearUintHistogram("linear uint", 0, 1, 10);
    list.emplace(std::move(val));
  }
  {
    auto val = root.CreateLinearDoubleHistogram("linear double", 0, 1, 10);
    list.emplace(std::move(val));
  }
  {
    auto val = root.CreateExponentialIntHistogram("exp int", 0, 1, 2, 10);
    list.emplace(std::move(val));
  }
  {
    auto val = root.CreateExponentialUintHistogram("exp uint", 0, 1, 2, 10);
    list.emplace(std::move(val));
  }
  {
    auto val = root.CreateExponentialDoubleHistogram("exp double", 0, 1, 2, 10);
    list.emplace(std::move(val));
  }

  auto result = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  EXPECT_EQ(1u, hierarchy.children().size());
  EXPECT_EQ(14u, hierarchy.node().properties().size());
}

}  // namespace
