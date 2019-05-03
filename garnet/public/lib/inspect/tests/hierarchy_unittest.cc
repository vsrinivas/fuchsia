// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/fit/defer.h>
#include <lib/inspect/inspect.h>
#include <lib/inspect/testing/inspect.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using inspect::Node;
using inspect::ObjectHierarchy;
using testing::AllOf;
using testing::ElementsAre;
using testing::IsEmpty;
using namespace inspect::testing;

// Convenience function for reading an ObjectHierarchy snapshot from a Tree.
ObjectHierarchy GetHierarchy(const inspect::Tree& tree) {
  zx::vmo duplicate;
  if (tree.GetVmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate) != ZX_OK) {
    return ObjectHierarchy();
  }
  auto ret = inspect::ReadFromVmo(std::move(duplicate));
  EXPECT_TRUE(ret.is_ok());
  if (ret.is_ok()) {
    return ret.take_value();
  }
  return ObjectHierarchy();
}

TEST(Hierarchy, Sorting) {
  auto tree = inspect::Inspector().CreateTree("test");
  auto& root = tree.GetRoot();

  auto string_sort_node = root.CreateChild("string_sort_node");
  auto s_1 = string_sort_node.CreateIntMetric("1", 1);
  auto s_2 = string_sort_node.CreateIntMetric("two", 2);
  auto s_3 = string_sort_node.CreateIntMetric("3", 3);
  auto s_one = string_sort_node.CreateStringProperty("1", "1");
  auto s_two = string_sort_node.CreateStringProperty("two", "2");
  auto s_three = string_sort_node.CreateStringProperty("3", "3");
  auto s_child1 = string_sort_node.CreateChild("1");
  auto s_child2 = string_sort_node.CreateChild("two");
  auto s_child3 = string_sort_node.CreateChild("3");

  auto numeric_sort_node = root.CreateChild("numeric_sort_node");
  auto n_1 = numeric_sort_node.CreateIntMetric("1", 1);
  auto n_222 = numeric_sort_node.CreateIntMetric("22", 22);
  auto n_3 = numeric_sort_node.CreateIntMetric("3", 3);
  auto n_one = numeric_sort_node.CreateStringProperty("1", "1");
  auto n_twotwo = numeric_sort_node.CreateStringProperty("22", "22");
  auto n_three = numeric_sort_node.CreateStringProperty("3", "3");
  auto n_child1 = numeric_sort_node.CreateChild("1");
  auto n_child22 = numeric_sort_node.CreateChild("22");
  auto n_child3 = numeric_sort_node.CreateChild("3");

  auto hierarchy = GetHierarchy(tree);
  hierarchy.Sort();

  EXPECT_THAT(
      hierarchy,
      ChildrenMatch(ElementsAre(
          AllOf(NodeMatches(AllOf(
                    NameMatches("numeric_sort_node"),
                    PropertyList(ElementsAre(StringPropertyIs("1", "1"),
                                             StringPropertyIs("3", "3"),
                                             StringPropertyIs("22", "22"))),
                    MetricList(ElementsAre(IntMetricIs("1", 1),
                                           IntMetricIs("3", 3),
                                           IntMetricIs("22", 22))))),
                ChildrenMatch(ElementsAre(NodeMatches(NameMatches("1")),
                                          NodeMatches(NameMatches("3")),
                                          NodeMatches(NameMatches("22"))))),
          AllOf(NodeMatches(AllOf(
                    NameMatches("string_sort_node"),
                    PropertyList(ElementsAre(StringPropertyIs("1", "1"),
                                             StringPropertyIs("3", "3"),
                                             StringPropertyIs("two", "2"))),
                    MetricList(ElementsAre(IntMetricIs("1", 1),
                                           IntMetricIs("3", 3),
                                           IntMetricIs("two", 2))))),
                ChildrenMatch(ElementsAre(NodeMatches(NameMatches("1")),
                                          NodeMatches(NameMatches("3")),
                                          NodeMatches(NameMatches("two"))))))));
}

}  // namespace
