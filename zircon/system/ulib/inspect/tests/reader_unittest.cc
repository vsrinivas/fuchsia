// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/single_threaded_executor.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <type_traits>

#include <zxtest/zxtest.h>

using inspect::Hierarchy;
using inspect::Inspector;
using inspect::MissingValueReason;
using inspect::Node;
using inspect::Snapshot;
using inspect::internal::Block;
using inspect::internal::BlockType;
using inspect::internal::ExtentBlockFields;
using inspect::internal::GetState;
using inspect::internal::HeaderBlockFields;
using inspect::internal::kMagicNumber;
using inspect::internal::kMinOrderSize;
using inspect::internal::LinkBlockDisposition;
using inspect::internal::NameBlockFields;
using inspect::internal::PropertyBlockPayload;
using inspect::internal::State;
using inspect::internal::ValueBlockFields;

namespace {

TEST(Reader, GetByPath) {
  Inspector inspector;
  ASSERT_TRUE(bool(inspector));
  auto child = inspector.GetRoot().CreateChild("test");
  auto child2 = child.CreateChild("test2");

  auto result = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  EXPECT_NOT_NULL(hierarchy.GetByPath({"test"}));
  EXPECT_NOT_NULL(hierarchy.GetByPath({"test", "test2"}));
  EXPECT_NULL(hierarchy.GetByPath({"test", "test2", "test3"}));
}

TEST(Reader, VisitHierarchy) {
  Inspector inspector;
  ASSERT_TRUE(bool(inspector));

  // root:
  //   test:
  //     test2
  //   test3
  auto child = inspector.GetRoot().CreateChild("test");
  auto child2 = child.CreateChild("test2");
  auto child3 = inspector.GetRoot().CreateChild("test3");

  auto result = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();
  hierarchy.Sort();

  std::vector<std::vector<std::string>> paths;
  hierarchy.Visit([&](const std::vector<std::string>& path, Hierarchy* current) {
    paths.push_back(path);
    EXPECT_NE(nullptr, current);
    return true;
  });

  std::vector<std::vector<std::string>> expected;
  expected.emplace_back(std::vector<std::string>{"root"});
  expected.emplace_back(std::vector<std::string>{"root", "test"});
  expected.emplace_back(std::vector<std::string>{"root", "test", "test2"});
  expected.emplace_back(std::vector<std::string>{"root", "test3"});
  EXPECT_EQ(expected, paths);

  paths.clear();
  hierarchy.Visit([&](const std::vector<std::string>& path, Hierarchy* current) {
    paths.push_back(path);
    EXPECT_NE(nullptr, current);
    return false;
  });
  EXPECT_EQ(1u, paths.size());
}

TEST(Reader, VisitHierarchyWithTombstones) {
  Inspector inspector;
  ASSERT_TRUE(bool(inspector));

  // root:
  //   test:
  //     test2
  auto child = inspector.GetRoot().CreateChild("test");
  auto child2 = child.CreateChild("test2");
  auto child3 = child2.CreateChild("test3");
  auto _prop = child2.CreateString("val", "test");
  // Delete node
  child2 = inspect::Node();

  auto result = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();
  hierarchy.Sort();

  std::vector<std::vector<std::string>> paths;
  hierarchy.Visit([&](const std::vector<std::string>& path, Hierarchy* current) {
    paths.push_back(path);
    EXPECT_NE(nullptr, current);
    return true;
  });

  std::vector<std::vector<std::string>> expected;
  expected.emplace_back(std::vector<std::string>{"root"});
  expected.emplace_back(std::vector<std::string>{"root", "test"});
  EXPECT_EQ(expected, paths);
}

TEST(Reader, BucketComparison) {
  inspect::UintArrayValue::HistogramBucket a(0, 2, 6);
  inspect::UintArrayValue::HistogramBucket b(0, 2, 6);
  inspect::UintArrayValue::HistogramBucket c(1, 2, 6);
  inspect::UintArrayValue::HistogramBucket d(0, 3, 6);
  inspect::UintArrayValue::HistogramBucket e(0, 2, 7);

  EXPECT_TRUE(a == b);
  EXPECT_TRUE(a != c);
  EXPECT_TRUE(b != c);
  EXPECT_TRUE(a != d);
  EXPECT_TRUE(a != e);
}

TEST(Reader, InvalidNameParsing) {
  std::vector<uint8_t> buf;
  buf.resize(4096);

  Block* header = reinterpret_cast<Block*>(buf.data());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], kMagicNumber, 4);
  header->payload.u64 = 0;

  // Manually create a value with an invalid name field.
  Block* value = reinterpret_cast<Block*>(buf.data() + kMinOrderSize);
  value->header = ValueBlockFields::Order::Make(0) |
                  ValueBlockFields::Type::Make(BlockType::kNodeValue) |
                  ValueBlockFields::NameIndex::Make(2000);

  auto result = inspect::ReadFromBuffer(std::move(buf));
  EXPECT_TRUE(result.is_ok());
}

TEST(Reader, LargeExtentsWithCycle) {
  std::vector<uint8_t> buf;
  buf.resize(4096);

  Block* header = reinterpret_cast<Block*>(buf.data());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], kMagicNumber, 4);
  header->payload.u64 = 0;

  // Manually create a property.
  Block* value = reinterpret_cast<Block*>(buf.data() + kMinOrderSize);
  value->header = ValueBlockFields::Order::Make(0) |
                  ValueBlockFields::Type::Make(BlockType::kBufferValue) |
                  ValueBlockFields::NameIndex::Make(2);
  value->payload.u64 = PropertyBlockPayload::TotalLength::Make(0xFFFFFFFF) |
                       PropertyBlockPayload::ExtentIndex::Make(3);

  Block* name = reinterpret_cast<Block*>(buf.data() + kMinOrderSize * 2);
  name->header = NameBlockFields::Order::Make(0) | NameBlockFields::Type::Make(BlockType::kName) |
                 NameBlockFields::Length::Make(1);
  memcpy(name->payload.data, "a", 2);

  Block* extent = reinterpret_cast<Block*>(buf.data() + kMinOrderSize * 3);
  extent->header = ExtentBlockFields::Order::Make(0) |
                   ExtentBlockFields::Type::Make(BlockType::kExtent) |
                   ExtentBlockFields::NextExtentIndex::Make(3);

  auto result = inspect::ReadFromBuffer(std::move(buf));
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(1u, result.value().node().properties().size());
}

TEST(Reader, NameDoesNotFit) {
  std::vector<uint8_t> buf;
  buf.resize(4096);

  Block* header = reinterpret_cast<Block*>(buf.data());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], kMagicNumber, 4);
  header->payload.u64 = 0;

  // Manually create a node.
  Block* value = reinterpret_cast<Block*>(buf.data() + kMinOrderSize);
  value->header = ValueBlockFields::Order::Make(0) |
                  ValueBlockFields::Type::Make(BlockType::kNodeValue) |
                  ValueBlockFields::NameIndex::Make(2);

  Block* name = reinterpret_cast<Block*>(buf.data() + kMinOrderSize * 2);
  name->header = NameBlockFields::Order::Make(0) | NameBlockFields::Type::Make(BlockType::kName) |
                 NameBlockFields::Length::Make(10);
  memcpy(name->payload.data, "a", 2);

  auto result = inspect::ReadFromBuffer(std::move(buf));
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(0u, result.value().children().size());
}

fit::result<Hierarchy> ReadHierarchyFromInspector(const Inspector& inspector) {
  fit::result<Hierarchy> result;
  fit::single_threaded_executor exec;
  exec.schedule_task(inspect::ReadFromInspector(inspector).then(
      [&](fit::result<Hierarchy>& res) { result = std::move(res); }));
  exec.run();

  return result;
}

TEST(Reader, MissingNamedChild) {
  Inspector inspector;
  auto state = GetState(&inspector);

  auto link =
      state->CreateLink("link", 0, "link-0", inspect::internal::LinkBlockDisposition::kChild);

  auto result = ReadHierarchyFromInspector(inspector);

  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();
  ASSERT_EQ(1, hierarchy.missing_values().size());
  EXPECT_EQ(MissingValueReason::kLinkNotFound, hierarchy.missing_values()[0].reason);
  EXPECT_EQ("link", hierarchy.missing_values()[0].name);
}

TEST(Reader, LinkedChildren) {
  Inspector inspector;
  auto state = GetState(&inspector);

  auto link0 = state->CreateLazyNode("link", 0, []() {
    inspect::Inspector inspect;
    inspect.GetRoot().CreateInt("val", 1, &inspect);
    return fit::make_ok_promise(inspect);
  });

  auto link1 = state->CreateLazyNode("other", 0, []() {
    inspect::Inspector inspect;
    inspect.GetRoot().CreateInt("val", 2, &inspect);
    return fit::make_ok_promise(inspect);
  });

  auto result = ReadHierarchyFromInspector(inspector);

  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();
  ASSERT_EQ(2, hierarchy.children().size());
  bool found_link = false, found_other = false;
  for (const auto& c : hierarchy.children()) {
    if (c.node().name() == "link") {
      ASSERT_EQ(1, c.node().properties().size());
      found_link = true;
      EXPECT_EQ("val", c.node().properties()[0].name());
      EXPECT_EQ(1, c.node().properties()[0].Get<inspect::IntPropertyValue>().value());
    } else if (c.node().name() == "other") {
      ASSERT_EQ(1, c.node().properties().size());
      found_other = true;
      EXPECT_EQ("val", c.node().properties()[0].name());
      EXPECT_EQ(2, c.node().properties()[0].Get<inspect::IntPropertyValue>().value());
    }
  }

  EXPECT_TRUE(found_link);
  EXPECT_TRUE(found_other);
}

TEST(Reader, LinkedInline) {
  Inspector inspector;
  auto state = GetState(&inspector);

  auto link = state->CreateLazyValues("link", 0, []() {
    inspect::Inspector inspector;
    inspector.GetRoot().CreateChild("child", &inspector);
    inspector.GetRoot().CreateInt("a", 10, &inspector);
    return fit::make_ok_promise(inspector);
  });

  auto result = ReadHierarchyFromInspector(inspector);
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  ASSERT_EQ(1, hierarchy.children().size());
  EXPECT_EQ("child", hierarchy.children()[0].node().name());
  ASSERT_EQ(1, hierarchy.node().properties().size());
  EXPECT_EQ("a", hierarchy.node().properties()[0].name());
  EXPECT_EQ(10, hierarchy.node().properties()[0].Get<inspect::IntPropertyValue>().value());
}

TEST(Reader, LinkedInlineChain) {
  Inspector inspector;
  auto state = GetState(&inspector);

  auto link = state->CreateLazyValues("link", 0, []() {
    inspect::Inspector inspector;
    inspector.GetRoot().CreateInt("a", 10, &inspector);
    inspector.GetRoot().CreateLazyValues(
        "link",
        []() {
          inspect::Inspector inspector;
          inspector.GetRoot().CreateInt("b", 11, &inspector);
          inspector.GetRoot().CreateLazyValues(
              "link",
              []() {
                inspect::Inspector inspector;
                inspector.GetRoot().CreateInt("c", 12, &inspector);
                return fit::make_ok_promise(inspector);
              },
              &inspector);
          return fit::make_ok_promise(inspector);
        },
        &inspector);
    return fit::make_ok_promise(inspector);
  });

  auto result = ReadHierarchyFromInspector(inspector);
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();
  hierarchy.Sort();

  ASSERT_EQ(0, hierarchy.children().size());
  ASSERT_EQ(3, hierarchy.node().properties().size());
  EXPECT_EQ("a", hierarchy.node().properties()[0].name());
  EXPECT_EQ("b", hierarchy.node().properties()[1].name());
  EXPECT_EQ("c", hierarchy.node().properties()[2].name());
  EXPECT_EQ(10, hierarchy.node().properties()[0].Get<inspect::IntPropertyValue>().value());
  EXPECT_EQ(11, hierarchy.node().properties()[1].Get<inspect::IntPropertyValue>().value());
  EXPECT_EQ(12, hierarchy.node().properties()[2].Get<inspect::IntPropertyValue>().value());
}

}  // namespace
