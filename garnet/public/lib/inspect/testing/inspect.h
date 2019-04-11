// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_TESTING_INSPECT_H_
#define LIB_INSPECT_TESTING_INSPECT_H_

#include "fuchsia/inspect/cpp/fidl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/inspect/inspect.h"
#include "lib/inspect/reader.h"

namespace inspect {

namespace hierarchy {
// Printers for inspect types.
void PrintTo(const Metric& metric, std::ostream* os);
void PrintTo(const Property& property, std::ostream* os);
void PrintTo(const Node& node, std::ostream* os);
}  // namespace hierarchy

// Printer for ObjectHierarchy wrapper.
void PrintTo(const ObjectHierarchy& hierarchy, std::ostream* os);

namespace testing {

// Type for a matcher matching a Node.
using NodeMatcher = ::testing::Matcher<const hierarchy::Node&>;

// Type for a matcher matching a vector of metrics.
using MetricsMatcher =
    ::testing::Matcher<const std::vector<hierarchy::Metric>&>;

// Type for a matcher matching a vector of properties.
using PropertiesMatcher =
    ::testing::Matcher<const std::vector<hierarchy::Property>&>;

// Type for a matcher that matches a base path on an |ObjectHierarchy|.
using PrefixPathMatcher = ::testing::Matcher<const std::vector<std::string>&>;

// Type for a matcher that matches a vector of |ObjectHierarchy| children.
using ChildrenMatcher = ::testing::Matcher<const std::vector<ObjectHierarchy>&>;

namespace internal {

// Matcher interface to check the name of an inspect Nodes.
class NameMatchesMatcher
    : public ::testing::MatcherInterface<const hierarchy::Node&> {
 public:
  NameMatchesMatcher(std::string name);

  bool MatchAndExplain(const hierarchy::Node& obj,
                       ::testing::MatchResultListener* listener) const override;

  void DescribeTo(::std::ostream* os) const override;

  void DescribeNegationTo(::std::ostream* os) const override;

 private:
  std::string name_;
};

// Matcher interface to check the list of Node metrics.
class MetricListMatcher
    : public ::testing::MatcherInterface<const hierarchy::Node&> {
 public:
  MetricListMatcher(MetricsMatcher matcher);

  bool MatchAndExplain(const hierarchy::Node& obj,
                       ::testing::MatchResultListener* listener) const override;

  void DescribeTo(::std::ostream* os) const override;

  void DescribeNegationTo(::std::ostream* os) const override;

 private:
  MetricsMatcher matcher_;
};

// Matcher interface to check the list of Node properties.
class PropertyListMatcher
    : public ::testing::MatcherInterface<const hierarchy::Node&> {
 public:
  PropertyListMatcher(PropertiesMatcher matcher);

  bool MatchAndExplain(const hierarchy::Node& obj,
                       ::testing::MatchResultListener* listener) const override;

  void DescribeTo(::std::ostream* os) const override;

  void DescribeNegationTo(::std::ostream* os) const override;

 private:
  PropertiesMatcher matcher_;
};

}  // namespace internal

// Matches against the name of an Inspect Node.
// Example:
//  EXPECT_THAT(node, NameMatches("objects"));
::testing::Matcher<const hierarchy::Node&> NameMatches(std::string name);

// Matches against the metric list of an Inspect Node.
// Example:
//  EXPECT_THAT(node, AllOf(MetricList(::testing::IsEmpty())));
::testing::Matcher<const hierarchy::Node&> MetricList(MetricsMatcher matcher);

// Matches against the property list of an Inspect Node.
// Example:
//  EXPECT_THAT(node, AllOf(PropertyList(::testing::IsEmpty())));
::testing::Matcher<const hierarchy::Node&> PropertyList(
    PropertiesMatcher matcher);

// Matches a particular StringProperty with the given name and value.
::testing::Matcher<const hierarchy::Property&> StringPropertyIs(
    const std::string& name, const std::string& value);

// Matches a particular ByteVectorProperty with the given name and value.
::testing::Matcher<const hierarchy::Property&> ByteVectorPropertyIs(
    const std::string& name, const VectorValue& value);

// Matches a particular IntMetric with the given name and value.
::testing::Matcher<const hierarchy::Metric&> IntMetricIs(
    const std::string& name, int64_t value);

// Matches a particular UIntMetric with the given name and value.
::testing::Matcher<const hierarchy::Metric&> UIntMetricIs(
    const std::string& name, uint64_t value);

// Matches a particular DoubleMetric with the given name and value.
::testing::Matcher<const hierarchy::Metric&> DoubleMetricIs(
    const std::string& name, double value);

// Matcher for the object inside an ObjectHierarchy.
::testing::Matcher<const ObjectHierarchy&> NodeMatches(NodeMatcher matcher);

// DEPRECATED: Compatibility for downstream clients.
// TODO(CF-702): Remove this.
::testing::Matcher<const ObjectHierarchy&> ObjectMatches(NodeMatcher matcher);

// Matcher for the base path inside an ObjectHierarchy.
::testing::Matcher<const ObjectHierarchy&> PrefixPathMatches(
    PrefixPathMatcher matcher);

// Matcher for the children of the object in an ObjectHierarchy.
::testing::Matcher<const ObjectHierarchy&> ChildrenMatch(
    ChildrenMatcher matcher);

}  // namespace testing
}  // namespace inspect

#endif  // LIB_INSPECT_TESTING_INSPECT_H_
