// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace_event/internal/categories_matcher.h"

#include "gtest/gtest.h"

namespace tracing {
namespace internal {
namespace {

TEST(CategoriesMatcherTest, EmptyState) {
  CategoriesMatcher cm;
  EXPECT_TRUE(cm.IsAnyCategoryEnabled("test"));
}

TEST(CategoriesMatcherTest, Reset) {
  CategoriesMatcher cm;
  cm.SetEnabledCategories({"enabled"});
  EXPECT_TRUE(cm.IsAnyCategoryEnabled("enabled"));
  EXPECT_FALSE(cm.IsAnyCategoryEnabled("disabled"));
  cm.Reset();
  EXPECT_TRUE(cm.IsAnyCategoryEnabled("enabled"));
  EXPECT_TRUE(cm.IsAnyCategoryEnabled("disabled"));
}

TEST(CategoriesMatcherTest, Matching) {
  CategoriesMatcher cm;
  cm.SetEnabledCategories({"one", "two", "three"});
  EXPECT_TRUE(cm.IsAnyCategoryEnabled(",one"));
  EXPECT_TRUE(cm.IsAnyCategoryEnabled("one"));
  EXPECT_TRUE(cm.IsAnyCategoryEnabled("two"));
  EXPECT_TRUE(cm.IsAnyCategoryEnabled("three"));
  EXPECT_FALSE(cm.IsAnyCategoryEnabled("four"));
  EXPECT_TRUE(cm.IsAnyCategoryEnabled("one,four"));
  EXPECT_TRUE(cm.IsAnyCategoryEnabled("two,four"));
  EXPECT_TRUE(cm.IsAnyCategoryEnabled("three,four"));
  EXPECT_FALSE(cm.IsAnyCategoryEnabled("four,four"));
  EXPECT_FALSE(cm.IsAnyCategoryEnabled(nullptr));
}

}  // namespace
}  // namespace internal
}  // namespace tracing
