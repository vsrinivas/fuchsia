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
  ValueWrapper(Node obj, int val)
      : object_(std::move(obj)),
        value_(object_.CreateIntMetric("value", val)) {}

 private:
  Node object_;
  inspect::IntMetric value_;
};

TEST(InspectVmo, Child) {
  auto tree = inspect::Inspector().CreateTree("root");
  Node& root = tree.GetRoot();
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
  Node& root = tree.GetRoot();
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

template <typename Type>
void DefaultArrayTest() {
  Type default_metric;
  default_metric.Add(0, 1);
  default_metric.Subtract(0, 1);
  default_metric.Set(0, 1);
}

template <typename Type>
void DefaultHistogramTest() {
  Type default_metric;
  default_metric.Insert(0);
}

TEST(InspectVmo, Metrics) {
  DefaultMetricTest<inspect::IntMetric>();
  DefaultMetricTest<inspect::UIntMetric>();
  DefaultMetricTest<inspect::DoubleMetric>();

  auto tree = inspect::Inspector().CreateTree("root");
  Node& root = tree.GetRoot();
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

TEST(InspectVmo, Arrays) {
  DefaultArrayTest<inspect::IntArray>();
  DefaultArrayTest<inspect::UIntArray>();
  DefaultArrayTest<inspect::DoubleArray>();

  auto tree = inspect::Inspector().CreateTree("root");
  Node& root = tree.GetRoot();
  {
    auto metric_int = root.CreateIntArray("int", 5);
    metric_int.Add(0, 5);
    metric_int.Subtract(2, 4);
    auto metric_uint = root.CreateUIntArray("uint", 5);
    metric_uint.Add(0, 5);
    metric_uint.Add(2, 5);
    metric_uint.Subtract(2, 4);
    auto metric_double = root.CreateDoubleArray("double", 5);
    metric_double.Add(0, 1);
    metric_double.Subtract(2, 0.5);
    EXPECT_THAT(
        GetHierarchy(tree),
        NodeMatches(AllOf(
            NameMatches("root"),
            MetricList(UnorderedElementsAre(
                IntArrayIs("int", ::testing::ElementsAre(5, 0, -4, 0, 0)),
                UIntArrayIs("uint", ::testing::ElementsAre(5, 0, 1, 0, 0)),
                DoubleArrayIs("double",
                              ::testing::ElementsAre(1, 0, -0.5, 0, 0)))))));
  }
  // Check that the metrics are removed when they goes out of scope.
  EXPECT_THAT(GetHierarchy(tree), NodeMatches(MetricList(IsEmpty())));
}

TEST(InspectVmo, LinearHistograms) {
  DefaultHistogramTest<inspect::LinearIntHistogramMetric>();
  DefaultHistogramTest<inspect::LinearUIntHistogramMetric>();
  DefaultHistogramTest<inspect::LinearDoubleHistogramMetric>();

  auto tree = inspect::Inspector().CreateTree("root");
  Node& root = tree.GetRoot();
  {
    auto metric_int = root.CreateLinearIntHistogramMetric("int", 10, 5, 5);
    metric_int.Insert(0, 2);
    metric_int.Insert(16);
    metric_int.Insert(230);
    auto expected_int_values = CreateExpectedLinearHistogramContents<int64_t>(
        10, 5, 5, {0, 0, 16, 230});
    EXPECT_THAT(expected_int_values,
                ::testing::ElementsAre(10, 5, 2, 0, 1, 0, 0, 0, 1));
    auto metric_uint = root.CreateLinearUIntHistogramMetric("uint", 10, 5, 5);
    metric_uint.Insert(0, 2);
    metric_uint.Insert(16);
    metric_uint.Insert(230);
    auto expected_uint_values = CreateExpectedLinearHistogramContents<uint64_t>(
        10, 5, 5, {0, 0, 16, 230});
    EXPECT_THAT(expected_uint_values,
                ::testing::ElementsAre(10, 5, 2, 0, 1, 0, 0, 0, 1));
    auto metric_double =
        root.CreateLinearDoubleHistogramMetric("double", 10, .5, 5);
    metric_double.Insert(0, 2);
    metric_double.Insert(11);
    metric_double.Insert(230);
    auto expected_double_values = CreateExpectedLinearHistogramContents<double>(
        10, .5, 5, {0, 0, 11, 230});
    EXPECT_THAT(expected_double_values,
                ::testing::ElementsAre(10, .5, 2, 0, 0, 1, 0, 0, 1));
    EXPECT_THAT(
        GetHierarchy(tree),
        NodeMatches(
            AllOf(NameMatches("root"),
                  MetricList(UnorderedElementsAre(
                      IntArrayIs("int", ::testing::Eq(expected_int_values)),
                      UIntArrayIs("uint", ::testing::Eq(expected_uint_values)),
                      DoubleArrayIs("double",
                                    ::testing::Eq(expected_double_values)))))));
  }
  // Check that the metrics are removed when they goes out of scope.
  EXPECT_THAT(GetHierarchy(tree), NodeMatches(MetricList(IsEmpty())));
}

TEST(InspectVmo, ExponentialHistograms) {
  DefaultHistogramTest<inspect::ExponentialIntHistogramMetric>();
  DefaultHistogramTest<inspect::ExponentialUIntHistogramMetric>();
  DefaultHistogramTest<inspect::ExponentialDoubleHistogramMetric>();

  auto tree = inspect::Inspector().CreateTree("root");
  Node& root = tree.GetRoot();
  {
    auto metric_int =
        root.CreateExponentialIntHistogramMetric("int", 1, 1, 2, 4);
    metric_int.Insert(0, 2);
    metric_int.Insert(8);
    metric_int.Insert(230);
    auto expected_int_values =
        CreateExpectedExponentialHistogramContents<int64_t>(1, 1, 2, 4,
                                                            {0, 0, 8, 230});
    EXPECT_THAT(expected_int_values,
                ::testing::ElementsAre(1, 1, 2, 2, 0, 0, 0, 1, 1));
    auto metric_uint =
        root.CreateExponentialUIntHistogramMetric("uint", 1, 1, 2, 4);
    metric_uint.Insert(0, 2);
    metric_uint.Insert(8);
    metric_uint.Insert(230);
    auto expected_uint_values =
        CreateExpectedExponentialHistogramContents<uint64_t>(1, 1, 2, 4,
                                                             {0, 0, 8, 230});
    EXPECT_THAT(expected_uint_values,
                ::testing::ElementsAre(1, 1, 2, 2, 0, 0, 0, 1, 1));
    auto metric_double =
        root.CreateExponentialDoubleHistogramMetric("double", 1, 1, 2, 4);
    metric_double.Insert(0, 2);
    metric_double.Insert(8);
    metric_double.Insert(230);
    auto expected_double_values =
        CreateExpectedExponentialHistogramContents<double>(1, 1, 2, 4,
                                                           {0, 0, 8, 230});
    EXPECT_THAT(expected_double_values,
                ::testing::ElementsAre(1, 1, 2, 2, 0, 0, 0, 1, 1));
    EXPECT_THAT(
        GetHierarchy(tree),
        NodeMatches(
            AllOf(NameMatches("root"),
                  MetricList(UnorderedElementsAre(
                      IntArrayIs("int", ::testing::Eq(expected_int_values)),
                      UIntArrayIs("uint", ::testing::Eq(expected_uint_values)),
                      DoubleArrayIs("double",
                                    ::testing::Eq(expected_double_values)))))));
  }
  // Check that the metrics are removed when they goes out of scope.
  EXPECT_THAT(GetHierarchy(tree), NodeMatches(MetricList(IsEmpty())));
}

TEST(InspectVmo, Properties) {
  auto tree = inspect::Inspector().CreateTree("root");
  Node& root = tree.GetRoot();
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
  Node& root = tree.GetRoot();
  {
    Node child_a = root.CreateChild("child_a");
    Node child_b = root.CreateChild("child_b");
    Node child_a_c = child_a.CreateChild("child_a_c");
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
