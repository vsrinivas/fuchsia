// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>

#include <sdk/lib/inspect/contrib/cpp/read_visitor.h>
#include <zxtest/zxtest.h>

namespace {

using inspect::Inspector;
using inspect::Node;
using inspect::contrib::VisitProperties;

// This macro creates the following tree inside the given inspector:
// root:
//   top_level = 1
//   test:
//     v1 = -1
//     v2 = 12u
//     v3 = -12
//     hist = [[0, 1) = 0, [1, 2) = 0, [2, 3) = 0, [3, 5) = 0, [5, 9) = 1, [9, max) = 0]
//   test2:
//     v4 = "Hello"
//     other_int = -1
//   test3:
//     v5 = "Goodbye"
#define CREATE_TEST_TREE(inspector)                                     \
  auto top_level = inspector.GetRoot().CreateInt("top_level", 1);       \
  auto child = inspector.GetRoot().CreateChild("test");                 \
  auto v1 = child.CreateInt("v1", -10);                                 \
  auto v2 = child.CreateUint("v2", 12);                                 \
  auto v3 = child.CreateInt("v3", -12);                                 \
  auto hist = child.CreateExponentialUintHistogram("hist", 1, 1, 2, 4); \
  hist.Insert(8);                                                       \
  auto child2 = child.CreateChild("test2");                             \
  auto v4 = child2.CreateString("v4", "Hello");                         \
  auto other_int = child2.CreateInt("other_int", -1);                   \
                                                                        \
  auto child3 = child.CreateChild("test3");                             \
  auto v5 = child3.CreateString("v5", "Goodbye");

TEST(ReaderTest, VisitPropertiesWildcard) {
  Inspector inspector;
  ASSERT_TRUE(static_cast<bool>(inspector));

  CREATE_TEST_TREE(inspector);

  auto result = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  fpromise::result<int64_t> v1_result, v3_result;
  bool found_other_match = false;

  EXPECT_TRUE(VisitProperties<inspect::IntPropertyValue>(
      hierarchy, {"test", inspect::contrib::kPathWildcard},
      [&](const std::vector<cpp17::string_view> path, const inspect::IntPropertyValue& value) {
        if (path.back() == "v1") {
          v1_result = fpromise::ok(value.value());
        } else if (path.back() == "v3") {
          v3_result = fpromise::ok(value.value());
        } else {
          found_other_match = true;
        }
      }));

  EXPECT_FALSE(found_other_match);
  ASSERT_TRUE(v1_result.is_ok());
  EXPECT_EQ(-10, v1_result.value());
  ASSERT_TRUE(v3_result.is_ok());
  EXPECT_EQ(-12, v3_result.value());
}

TEST(ReaderTest, VisitPropertiesExact) {
  Inspector inspector;
  ASSERT_TRUE(static_cast<bool>(inspector));

  CREATE_TEST_TREE(inspector);

  auto result = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  fpromise::result<uint64_t> v2_result;
  EXPECT_TRUE(VisitProperties<inspect::UintPropertyValue>(
      hierarchy, {"test", "v2"},
      [&](const std::vector<cpp17::string_view> path, const inspect::UintPropertyValue& value) {
        v2_result = fpromise::ok(value.value());
      }));
  ASSERT_TRUE(v2_result.is_ok());
  EXPECT_EQ(12, v2_result.value());
}

TEST(ReaderTest, VisitPropertiesHistogram) {
  Inspector inspector;
  ASSERT_TRUE(static_cast<bool>(inspector));

  CREATE_TEST_TREE(inspector);

  auto result = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  fpromise::result<std::vector<inspect::UintArrayValue::HistogramBucket>> hist_result;
  EXPECT_TRUE(VisitProperties<inspect::UintArrayValue>(
      hierarchy, {"test", "hist"},
      [&](const std::vector<cpp17::string_view> path, const inspect::UintArrayValue& value) {
        hist_result = fpromise::ok(value.GetBuckets());
      }));
  ASSERT_TRUE(hist_result.is_ok());
  ASSERT_EQ(6, hist_result.value().size());
  EXPECT_EQ(inspect::UintArrayValue::HistogramBucket(0, 1, 0), hist_result.value()[0]);
  EXPECT_EQ(inspect::UintArrayValue::HistogramBucket(1, 2, 0), hist_result.value()[1]);
  EXPECT_EQ(inspect::UintArrayValue::HistogramBucket(2, 3, 0), hist_result.value()[2]);
  EXPECT_EQ(inspect::UintArrayValue::HistogramBucket(3, 5, 0), hist_result.value()[3]);
  EXPECT_EQ(inspect::UintArrayValue::HistogramBucket(5, 9, 1), hist_result.value()[4]);
  EXPECT_EQ(inspect::UintArrayValue::HistogramBucket(9, ULLONG_MAX, 0), hist_result.value()[5]);
}

TEST(ReaderTest, VisitPropertiesRecursive) {
  Inspector inspector;
  ASSERT_TRUE(static_cast<bool>(inspector));

  CREATE_TEST_TREE(inspector);

  auto result = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  fpromise::result<std::string> v4_result;
  fpromise::result<std::string> v5_result;
  bool found_other_match = false;
  EXPECT_TRUE(VisitProperties<inspect::StringPropertyValue>(
      hierarchy, {"test", inspect::contrib::kPathWildcard, inspect::contrib::kPathRecursive},
      [&](const std::vector<cpp17::string_view> path, const inspect::StringPropertyValue& value) {
        if (path.back() == "v4") {
          v4_result = fpromise::ok(value.value());
        }
        if (path.back() == "v5") {
          v5_result = fpromise::ok(value.value());
        }
      }));
  EXPECT_FALSE(found_other_match);
  ASSERT_TRUE(v4_result.is_ok());
  ASSERT_TRUE(v5_result.is_ok());
  EXPECT_STREQ("Hello", v4_result.value());
  EXPECT_STREQ("Goodbye", v5_result.value());
}

TEST(ReaderTest, VisitPropertiesAllRecursive) {
  Inspector inspector;
  ASSERT_TRUE(static_cast<bool>(inspector));

  CREATE_TEST_TREE(inspector);

  auto result = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  size_t count_found = 0;
  EXPECT_TRUE(VisitProperties<inspect::IntPropertyValue>(
      hierarchy, {inspect::contrib::kPathRecursive},
      [&](const std::vector<cpp17::string_view> path, const inspect::IntPropertyValue& value) {
        count_found++;
      }));
  EXPECT_EQ(4, count_found);
}

TEST(ReaderTest, VisitPropertiesInvalidRecursiveWildcard) {
  Inspector inspector;
  ASSERT_TRUE(static_cast<bool>(inspector));

  CREATE_TEST_TREE(inspector);

  auto result = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  // Recursive wildcard must be at end.
  size_t count_found = 0;
  EXPECT_FALSE(VisitProperties<inspect::IntPropertyValue>(
      hierarchy, {inspect::contrib::kPathRecursive, "v1"},
      [&](const std::vector<cpp17::string_view> path, const inspect::IntPropertyValue& value) {
        count_found++;
      }));
  EXPECT_EQ(0, count_found);

  // Calling without path should be OK.
  EXPECT_FALSE(VisitProperties<inspect::IntPropertyValue>(
      hierarchy, {},
      [&](const std::vector<cpp17::string_view> path, const inspect::IntPropertyValue& value) {}));
}

}  // namespace
