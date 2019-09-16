// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>

#include <type_traits>

#include <zxtest/zxtest.h>

using inspect::Inspector;
using inspect::Node;

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

}  // namespace
