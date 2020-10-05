// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/single_threaded_executor.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>

#include <type_traits>

#include <zxtest/zxtest.h>

using inspect::Inspector;
using inspect::Node;

namespace {

TEST(Inspect, CreateDeleteActive) {
  Node node;

  {
    auto inspector = std::make_unique<Inspector>();
    EXPECT_TRUE(inspector->DuplicateVmo().get() != ZX_HANDLE_INVALID);
    EXPECT_TRUE(bool(*inspector));
    node = inspector->GetRoot().CreateChild("node");
    Node child = node.CreateChild("child");
    EXPECT_TRUE(bool(child));
  }

  EXPECT_TRUE(bool(node));

  Node child = node.CreateChild("child");
  EXPECT_TRUE(bool(child));
}

TEST(Inspect, VmoName) {
  char name[ZX_MAX_NAME_LEN];
  auto inspector = std::make_unique<Inspector>();
  auto state = inspect::internal::GetState(inspector.get());
  EXPECT_OK(state->GetVmo().get_property(ZX_PROP_NAME, name, sizeof(name)));
  EXPECT_EQ(std::string(name), "InspectHeap");
}

TEST(Inspect, CreateChildren) {
  auto inspector = std::make_unique<Inspector>();
  Node child = inspector->GetRoot().CreateChild("child");
  EXPECT_TRUE(bool(child));

  auto result = inspect::ReadFromVmo(inspector->DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  ASSERT_EQ(1u, hierarchy.children().size());
  EXPECT_EQ("child", hierarchy.children()[0].name());
}

TEST(Inspect, CreateCopyVmo) {
  auto inspector = std::make_unique<Inspector>();

  // Store a string.
  std::string s = "abcd";
  auto property = inspector->GetRoot().CreateString("string", s);
  auto result = inspect::ReadFromVmo(inspector->CopyVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  auto* string_value = hierarchy.node().get_property<inspect::StringPropertyValue>("string");
  ASSERT_TRUE(string_value != nullptr);
  EXPECT_EQ(s, string_value->value());
}

constexpr size_t kPageSize = 4096;

TEST(Inspect, CreateGetStats) {
  // Limit to 2 pages.
  Inspector inspector(inspect::InspectSettings{.maximum_size = 2 * kPageSize});

  auto stats = inspector.GetStats();
  EXPECT_EQ(1 * kPageSize, stats.size);
  EXPECT_EQ(2 * kPageSize, stats.maximum_size);
  EXPECT_EQ(0, stats.dynamic_child_count);

  // Fill up the buffer
  for (int i = 0; i < 1000; i++) {
    inspector.GetRoot().CreateString(std::to_string(i), "This is a test", &inspector);
  }

  stats = inspector.GetStats();
  EXPECT_EQ(2 * kPageSize, stats.size);
  EXPECT_EQ(2 * kPageSize, stats.maximum_size);
  EXPECT_EQ(0, stats.dynamic_child_count);
}

TEST(Inspect, GetLinks) {
  Inspector inspector;

  inspector.GetRoot().CreateLazyNode(
      "lazy",
      [] {
        Inspector insp;
        insp.GetRoot().CreateInt("val", 10, &insp);
        return fit::make_ok_promise(insp);
      },
      &inspector);

  auto children = inspector.GetChildNames();
  ASSERT_EQ(1u, children.size());
  EXPECT_EQ("lazy-0", children[0]);

  auto stats = inspector.GetStats();
  EXPECT_EQ(1u, stats.dynamic_child_count);

  fit::result<Inspector> result;
  fit::single_threaded_executor exec;
  exec.schedule_task(inspector.OpenChild("lazy-0").then(
      [&](fit::result<Inspector>& res) { result = std::move(res); }));
  exec.run();
  EXPECT_TRUE(result.is_ok());
}

TEST(Inspect, CreateCopyBytes) {
  // Make a 16MB heap.
  auto inspector = std::make_unique<Inspector>();

  // Store a string.
  std::string s = "abcd";
  auto property = inspector->GetRoot().CreateString("string", s);

  auto bytes = inspector->CopyBytes();
  auto result = inspect::ReadFromBuffer(std::move(bytes));
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  EXPECT_EQ(s, hierarchy.node().properties()[0].Get<inspect::StringPropertyValue>().value());
}

TEST(Inspect, CreateLargeHeap) {
  // Make a 16MB heap.
  auto inspector =
      std::make_unique<Inspector>(inspect::InspectSettings{.maximum_size = 16 * 1024 * 1024});

  // Store a 4MB string.
  std::string s(4 * 1024 * 1024, 'a');
  auto property = inspector->GetRoot().CreateString("big_string", s);
  auto result = inspect::ReadFromVmo(inspector->DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  EXPECT_EQ(s, hierarchy.node().properties()[0].Get<inspect::StringPropertyValue>().value());
}

TEST(Inspect, CreateInvalidSize) {
  auto inspector = std::make_unique<Inspector>(inspect::InspectSettings{.maximum_size = 0});
  EXPECT_TRUE(inspector->DuplicateVmo().get() == ZX_HANDLE_INVALID);
  EXPECT_FALSE(bool(inspector->GetRoot()));
  EXPECT_FALSE(bool(*inspector));
}

TEST(Inspect, CreateWithVmoInvalidSize) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(0 /* size */, 0, &vmo));
  Inspector inspector(std::move(vmo));
  auto stats = inspector.GetStats();
  EXPECT_EQ(0, stats.size);
  EXPECT_EQ(0, stats.maximum_size);
  EXPECT_EQ(0, stats.dynamic_child_count);
  EXPECT_FALSE(bool(inspector));
}

TEST(Inspect, CreateWithVmoReadOnly) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(4096 /* size */, 0, &vmo));

  zx::vmo duplicate;
  ASSERT_OK(vmo.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ, &duplicate));
  Inspector inspector(std::move(duplicate));
  auto stats = inspector.GetStats();
  EXPECT_EQ(0, stats.size);
  EXPECT_EQ(0, stats.maximum_size);
  EXPECT_EQ(0, stats.dynamic_child_count);
  EXPECT_FALSE(bool(inspector));
}

TEST(Inspect, CreateWithVmoDuplicateVmo) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(4096 /* size */, 0, &vmo));

  zx::vmo duplicate;
  ASSERT_OK(
      vmo.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP, &duplicate));
  Inspector inspector(std::move(duplicate));
  auto stats = inspector.GetStats();
  EXPECT_EQ(4096, stats.size);
  EXPECT_EQ(4096, stats.maximum_size);
  EXPECT_TRUE(bool(inspector));
}

TEST(Inspect, CreateWithDirtyVmo) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(4096 /* size */, 0, &vmo));

  // Write data into the VMO before using it, internally we will decommit
  // the pages to zero them.
  std::vector<uint8_t> bytes(4096, 'a');
  ASSERT_OK(vmo.write(bytes.data(), 0, bytes.size()));

  Inspector inspector(std::move(vmo));
  ASSERT_TRUE(bool(inspector));
  auto val = inspector.GetRoot().CreateUint("test", 100);

  auto result = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  EXPECT_EQ(1, hierarchy.node().properties().size());
  EXPECT_EQ("test", hierarchy.node().properties()[0].name());
  EXPECT_EQ(100, hierarchy.node().properties()[0].Get<inspect::UintPropertyValue>().value());

  auto stats = inspector.GetStats();
  EXPECT_EQ(4096, stats.size);
  EXPECT_EQ(4096, stats.maximum_size);
}

TEST(Inspect, UniqueName) {
  inspect::Inspector inspector1, inspector2;
  EXPECT_EQ("root0x0", inspector1.GetRoot().UniqueName("root"));
  EXPECT_EQ("root0x1", inspector1.GetRoot().UniqueName("root"));
  EXPECT_EQ("root0x2", inspector1.GetRoot().UniqueName("root"));
  EXPECT_EQ("test0x3", inspector1.GetRoot().UniqueName("test"));
  EXPECT_EQ("test0x4", inspector1.GetRoot().UniqueName("test"));
  EXPECT_EQ("test0x5", inspector1.GetRoot().UniqueName("test"));
  EXPECT_EQ("test0x6", inspector1.GetRoot().UniqueName("test"));
  EXPECT_EQ("test0x7", inspector1.GetRoot().UniqueName("test"));
  EXPECT_EQ("test0x8", inspector1.GetRoot().UniqueName("test"));
  EXPECT_EQ("test0x9", inspector1.GetRoot().UniqueName("test"));
  EXPECT_EQ("test0xa", inspector1.GetRoot().UniqueName("test"));
  EXPECT_EQ("test0xb", inspector1.GetRoot().UniqueName("test"));
  EXPECT_EQ("test0xc", inspector1.GetRoot().UniqueName("test"));
  EXPECT_EQ("test0xd", inspector1.GetRoot().UniqueName("test"));
  EXPECT_EQ("test0xe", inspector1.GetRoot().UniqueName("test"));
  EXPECT_EQ("test0xf", inspector1.GetRoot().UniqueName("test"));
  EXPECT_EQ("test0x10", inspector1.GetRoot().UniqueName("test"));

  EXPECT_EQ("root0x0", inspector2.GetRoot().UniqueName("root"));
}

}  // namespace
