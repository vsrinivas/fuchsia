// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/inspect.h>

#include <type_traits>

#include <zxtest/zxtest.h>

#include "lib/inspect/cpp/hierarchy.h"
#include "lib/inspect/cpp/reader.h"

using inspect::Inspector;
using inspect::Node;

namespace {

TEST(Inspect, CreateDeleteActive) {
  Node node;

  {
    auto inspector = std::make_unique<Inspector>("root");
    EXPECT_TRUE(inspector->GetVmo().is_ok());
    EXPECT_TRUE(bool(*inspector));
    node = inspector->GetRoot().CreateChild("node");
    Node child = node.CreateChild("child");
    EXPECT_TRUE(bool(child));
  }

  EXPECT_TRUE(bool(node));

  Node child = node.CreateChild("child");
  EXPECT_TRUE(bool(child));
}

TEST(Inspect, CreateLargeHeap) {
  // Make a 16MB heap.
  auto inspector = std::make_unique<Inspector>(
      "root", inspect::InspectSettings{.maximum_size = 16 * 1024 * 1024});

  // Store a 4MB string.
  std::string s(4 * 1024 * 1024, 'a');
  auto property = inspector->GetRoot().CreateString("big_string", s);
  auto result = inspect::ReadFromVmo(*inspector->GetVmo().take_value());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  EXPECT_EQ(s, hierarchy.node().properties()[0].Get<inspect::StringPropertyValue>().value());
}

TEST(Inspect, CreateInvalidSize) {
  auto inspector = std::make_unique<Inspector>("root", inspect::InspectSettings{.maximum_size = 0});
  EXPECT_FALSE(inspector->GetVmo().is_ok());
  EXPECT_FALSE(bool(inspector->GetRoot()));
  EXPECT_FALSE(bool(*inspector));
}

TEST(Inspect, CreateWithVmoInvalidSize) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(0 /* size */, 0, &vmo));
  Inspector inspector("root", std::move(vmo));
  EXPECT_FALSE(bool(inspector));
}

TEST(Inspect, CreateWithVmoReadOnly) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(4096 /* size */, 0, &vmo));

  zx::vmo duplicate;
  ASSERT_OK(vmo.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ, &duplicate));
  Inspector inspector("root", std::move(duplicate));
  EXPECT_FALSE(bool(inspector));
}

TEST(Inspect, CreateWithVmoDuplicate) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(4096 /* size */, 0, &vmo));

  zx::vmo duplicate;
  ASSERT_OK(
      vmo.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP, &duplicate));
  Inspector inspector("root", std::move(duplicate));
  EXPECT_TRUE(bool(inspector));
}

TEST(Inspect, CreateWithDirtyVmo) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(4096 /* size */, 0, &vmo));

  // Write data into the VMO before using it, internally we will decommit
  // the pages to zero them.
  std::vector<uint8_t> bytes(4096, 'a');
  ASSERT_OK(vmo.write(bytes.data(), 0, bytes.size()));

  Inspector inspector("root", std::move(vmo));
  ASSERT_TRUE(bool(inspector));
  auto val = inspector.GetRoot().CreateUint("test", 100);

  auto result = inspect::ReadFromVmo(*inspector.GetVmo().take_value());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  EXPECT_EQ(1, hierarchy.node().properties().size());
  EXPECT_EQ("test", hierarchy.node().properties()[0].name());
  EXPECT_EQ(100, hierarchy.node().properties()[0].Get<inspect::UintPropertyValue>().value());
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
