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

using inspect::Object;
using inspect::ObjectHierarchy;
using testing::AllOf;
using testing::IsEmpty;
using testing::UnorderedElementsAre;
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

TEST(InspectVmo, Object) {
  auto tree = inspect::Inspector().CreateTree("test");
  EXPECT_THAT(GetHierarchy(tree),
              NodeMatches(AllOf(NameMatches("test"), PropertyList(IsEmpty()),
                                MetricList(IsEmpty()))));
}

class ValueWrapper {
 public:
  ValueWrapper(Object obj, int val)
      : object_(std::move(obj)),
        value_(object_.CreateIntMetric("value", val)) {}

 private:
  Object object_;
  inspect::IntMetric value_;
};

TEST(InspectVmo, Child) {
  auto tree = inspect::Inspector().CreateTree("root");
  Object& root = tree.GetRoot();
  {
    // Create a child and check it exists.
    auto obj = root.CreateChild("child");
    EXPECT_THAT(
        GetHierarchy(tree),
        ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("child")))));

    auto obj2 = root.CreateChild("child2");
    EXPECT_THAT(GetHierarchy(tree), ChildrenMatch(UnorderedElementsAre(
                                        NodeMatches(NameMatches("child")),
                                        NodeMatches(NameMatches("child2")))));

    // Check assignment removes the old object.
    obj = root.CreateChild("newchild");
    EXPECT_THAT(GetHierarchy(tree), ChildrenMatch(UnorderedElementsAre(
                                        NodeMatches(NameMatches("newchild")),
                                        NodeMatches(NameMatches("child2")))));
  }
  // Check that the child is removed when it goes out of scope.
  EXPECT_THAT(GetHierarchy(tree), ChildrenMatch(IsEmpty()));
}

TEST(InspectVmo, ChildChaining) {
  auto tree = inspect::Inspector().CreateTree("root");
  Object& root = tree.GetRoot();
  {
    ValueWrapper v(root.CreateChild("child"), 100);
    EXPECT_THAT(
        GetHierarchy(tree),
        ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
            NameMatches("child"),
            MetricList(UnorderedElementsAre(IntMetricIs("value", 100))))))));
  }
  // Check that the child is removed when it goes out of scope.
  EXPECT_THAT(GetHierarchy(tree), ChildrenMatch(IsEmpty()));
}

template <typename Type>
void DefaultMetricTest() {
  Type default_metric;
  default_metric.Add(1);
  default_metric.Subtract(1);
  default_metric.Set(1);
}

TEST(InspectVmo, Metrics) {
  DefaultMetricTest<inspect::IntMetric>();
  DefaultMetricTest<inspect::UIntMetric>();
  DefaultMetricTest<inspect::DoubleMetric>();

  auto tree = inspect::Inspector().CreateTree("root");
  Object& root = tree.GetRoot();
  {
    auto metric_int = root.CreateIntMetric("int", -10);
    metric_int.Add(5);
    metric_int.Subtract(4);
    auto metric_uint = root.CreateUIntMetric("uint", 10);
    metric_uint.Add(4);
    metric_uint.Subtract(5);
    auto metric_double = root.CreateDoubleMetric("double", 0.25);
    metric_double.Add(1);
    metric_double.Subtract(0.5);
    EXPECT_THAT(
        GetHierarchy(tree),
        NodeMatches(AllOf(NameMatches("root"),
                          MetricList(UnorderedElementsAre(
                              IntMetricIs("int", -9), UIntMetricIs("uint", 9),
                              DoubleMetricIs("double", 0.75))))));
  }
  // Check that the metrics are removed when they goes out of scope.
  EXPECT_THAT(GetHierarchy(tree), NodeMatches(MetricList(IsEmpty())));
}

TEST(InspectVmo, Properties) {
  auto tree = inspect::Inspector().CreateTree("root");
  Object& root = tree.GetRoot();
  {
    auto property_string = root.CreateStringProperty("str", "test");
    property_string.Set("valid");
    auto property_vector =
        root.CreateByteVectorProperty("vec", inspect::VectorValue(3, 'a'));
    property_vector.Set(inspect::VectorValue(3, 'b'));
    EXPECT_THAT(
        GetHierarchy(tree),
        NodeMatches(AllOf(
            NameMatches("root"),
            PropertyList(UnorderedElementsAre(
                StringPropertyIs("str", "valid"),
                ByteVectorPropertyIs("vec", inspect::VectorValue(3, 'b')))))));
  }
  // Check that the properties are removed when they goes out of scope.
  EXPECT_THAT(GetHierarchy(tree), NodeMatches(PropertyList(IsEmpty())));
}

TEST(InspectVmo, NestedValues) {
  auto tree = inspect::Inspector().CreateTree("root");
  Object& root = tree.GetRoot();
  {
    Object child_a = root.CreateChild("child_a");
    Object child_b = root.CreateChild("child_b");
    Object child_a_c = child_a.CreateChild("child_a_c");
    auto property_string = root.CreateStringProperty("str", "test");
    property_string.Set("valid");
    auto property_vector =
        root.CreateByteVectorProperty("vec", inspect::VectorValue(3, 'a'));

    auto a_value = child_a.CreateIntMetric("value", -10);
    auto b_prop = child_b.CreateStringProperty("version", "1.0");
    auto a_c_value = child_a_c.CreateDoubleMetric("volume", 0.25);

    EXPECT_THAT(
        GetHierarchy(tree),
        AllOf(
            NodeMatches(AllOf(NameMatches("root"),
                              PropertyList(UnorderedElementsAre(
                                  StringPropertyIs("str", "valid"),
                                  ByteVectorPropertyIs(
                                      "vec", inspect::VectorValue(3, 'a')))))),
            ChildrenMatch(UnorderedElementsAre(
                AllOf(NodeMatches(AllOf(NameMatches("child_a"),
                                        MetricList(UnorderedElementsAre(
                                            IntMetricIs("value", -10))))),
                      ChildrenMatch(UnorderedElementsAre(AllOf(NodeMatches(
                          AllOf(NameMatches("child_a_c"),
                                MetricList(UnorderedElementsAre(
                                    DoubleMetricIs("volume", 0.25))))))))),
                NodeMatches(
                    AllOf(NameMatches("child_b"),
                          PropertyList(UnorderedElementsAre(
                              StringPropertyIs("version", "1.0")))))))));
  }
  // Check that the properties are removed when they goes out of scope.
  EXPECT_THAT(GetHierarchy(tree), NodeMatches(PropertyList(IsEmpty())));
}
}  // namespace
