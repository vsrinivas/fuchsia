// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <type_traits>

#include <lib/inspect/cpp/inspect.h>
#include <zxtest/zxtest.h>

using inspect::Inspector;
using inspect::Node;

namespace {

TEST(Inspect, CreateDeleteActive) {
  Node node;

  {
    auto inspector = std::make_unique<Inspector>("root");
    EXPECT_TRUE(inspector->GetVmo().is_ok());
    node = inspector->GetRoot().CreateChild("node");
    Node child = node.CreateChild("child");
    EXPECT_TRUE(bool(child));
  }

  EXPECT_TRUE(bool(node));

  Node child = node.CreateChild("child");
  EXPECT_TRUE(bool(child));
}

TEST(Inspect, CreateInvalidSize) {
  auto inspector = std::make_unique<Inspector>("root", inspect::InspectSettings{.maximum_size = 0});
  EXPECT_FALSE(inspector->GetVmo().is_ok());
  EXPECT_FALSE(bool(inspector->GetRoot()));
}

TEST(Inspect, UniqueName) {
  EXPECT_EQ("root0x0", inspect::UniqueName("root"));
  EXPECT_EQ("root0x1", inspect::UniqueName("root"));
  EXPECT_EQ("root0x2", inspect::UniqueName("root"));
  EXPECT_EQ("test0x3", inspect::UniqueName("test"));
  EXPECT_EQ("test0x4", inspect::UniqueName("test"));
  EXPECT_EQ("test0x5", inspect::UniqueName("test"));
  EXPECT_EQ("test0x6", inspect::UniqueName("test"));
  EXPECT_EQ("test0x7", inspect::UniqueName("test"));
  EXPECT_EQ("test0x8", inspect::UniqueName("test"));
  EXPECT_EQ("test0x9", inspect::UniqueName("test"));
  EXPECT_EQ("test0xa", inspect::UniqueName("test"));
  EXPECT_EQ("test0xb", inspect::UniqueName("test"));
  EXPECT_EQ("test0xc", inspect::UniqueName("test"));
  EXPECT_EQ("test0xd", inspect::UniqueName("test"));
  EXPECT_EQ("test0xe", inspect::UniqueName("test"));
  EXPECT_EQ("test0xf", inspect::UniqueName("test"));
  EXPECT_EQ("test0x10", inspect::UniqueName("test"));
}

}  // namespace
