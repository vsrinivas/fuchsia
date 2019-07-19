// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_TESTING_CPP_INSPECT_H_
#define LIB_INSPECT_TESTING_CPP_INSPECT_H_

#include <lib/inspect/cpp/reader.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace inspect {

// Printers for inspect types.
void PrintTo(const PropertyValue& property, std::ostream* os);
void PrintTo(const NodeValue& node, std::ostream* os);

// Printer for Hierarchy wrapper.
void PrintTo(const Hierarchy& hierarchy, std::ostream* os);

namespace testing {

// Type for a matcher matching a Node.
using NodeMatcher = ::testing::Matcher<const NodeValue&>;

// Type for a matcher matching a vector of properties.
using PropertiesMatcher = ::testing::Matcher<const std::vector<PropertyValue>&>;

// Type for a matcher that matches a base path on an |Hierarchy|.
using PrefixPathMatcher = ::testing::Matcher<const std::vector<std::string>&>;

// Type for a matcher that matches a vector of |Hierarchy| children.
using ChildrenMatcher = ::testing::Matcher<const std::vector<Hierarchy>&>;

namespace internal {

// Matcher interface to check the name of an inspect Nodes.
class NameMatchesMatcher : public ::testing::MatcherInterface<const NodeValue&> {
 public:
  NameMatchesMatcher(std::string name);

  bool MatchAndExplain(const NodeValue& obj,
                       ::testing::MatchResultListener* listener) const override;

  void DescribeTo(::std::ostream* os) const override;

  void DescribeNegationTo(::std::ostream* os) const override;

 private:
  std::string name_;
};

// Matcher interface to check the list of Node properties.
class PropertyListMatcher : public ::testing::MatcherInterface<const NodeValue&> {
 public:
  PropertyListMatcher(PropertiesMatcher matcher);

  bool MatchAndExplain(const NodeValue& obj,
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
::testing::Matcher<const NodeValue&> NameMatches(std::string name);

// Matches against the property list of an Inspect Node.
// Example:
//  EXPECT_THAT(node, AllOf(PropertyList(::testing::IsEmpty())));
::testing::Matcher<const NodeValue&> PropertyList(PropertiesMatcher matcher);

// Matches a particular StringProperty with the given name and value.
::testing::Matcher<const PropertyValue&> StringIs(const std::string& name,
                                                  const std::string& value);

// Matches a particular ByteVectorProperty with the given name and value.
::testing::Matcher<const PropertyValue&> ByteVectorIs(const std::string& name,
                                                      const std::vector<uint8_t>& value);

// Matches a particular IntProperty with the given name and value.
::testing::Matcher<const PropertyValue&> IntIs(const std::string& name, int64_t value);

// Matches a particular UintProperty with the given name and value.
::testing::Matcher<const PropertyValue&> UintIs(const std::string& name, uint64_t value);

// Matches a particular DoubleProperty with the given name and value.
::testing::Matcher<const PropertyValue&> DoubleIs(const std::string& name, double value);

// Matches the values of an integer array.
::testing::Matcher<const PropertyValue&> IntArrayIs(const std::string& name,
                                                    ::testing::Matcher<std::vector<int64_t>>);

// Matches the values of an unsigned integer array.
::testing::Matcher<const PropertyValue&> UintArrayIs(const std::string& name,
                                                     ::testing::Matcher<std::vector<uint64_t>>);

// Matches the values of a double width floating point number array.
::testing::Matcher<const PropertyValue&> DoubleArrayIs(const std::string& name,
                                                       ::testing::Matcher<std::vector<double>>);

// Matches the display format of a numeric array value.
::testing::Matcher<const PropertyValue&> ArrayDisplayFormatIs(ArrayDisplayFormat format);

// Matcher for the object inside an Hierarchy.
::testing::Matcher<const Hierarchy&> NodeMatches(NodeMatcher matcher);

// DEPRECATED: Compatibility for downstream clients.
// TODO(CF-702): Remove this.
::testing::Matcher<const Hierarchy&> ObjectMatches(NodeMatcher matcher);

// Matcher for the base path inside an Hierarchy.
::testing::Matcher<const Hierarchy&> PrefixPathMatches(PrefixPathMatcher matcher);

// Matcher for the children of the object in an Hierarchy.
::testing::Matcher<const Hierarchy&> ChildrenMatch(ChildrenMatcher matcher);

// Computes the bucket index for a value in a linear histogram.
template <typename T>
size_t ComputeLinearBucketIndex(T floor, T step_size, size_t buckets, T value) {
  size_t ret;
  for (ret = 0; value >= floor && ret < buckets + 1; floor += step_size, ret++) {
  }
  return ret;
}

// Computes the bucket index for a value in an exponential histogram.
template <typename T>
size_t ComputeExponentialBucketIndex(T floor, T initial_step, T step_multiplier, size_t buckets,
                                     T value) {
  T current_step = initial_step;
  size_t ret;
  for (ret = 0; value >= floor && ret < buckets + 1;
       floor += current_step, current_step *= step_multiplier, ret++) {
  }
  return ret;
}

// Creates the expected contents of a linear histogram given parameters and
// values.
template <typename T>
std::vector<T> CreateExpectedLinearHistogramContents(T floor, T step_size, size_t buckets,
                                                     const std::vector<T>& values) {
  const size_t underflow_bucket_offset = 2;
  const size_t array_size = 4 + buckets;
  std::vector<T> expected = {floor, step_size};
  expected.resize(array_size);
  for (T value : values) {
    expected[underflow_bucket_offset +
             ComputeLinearBucketIndex(floor, step_size, buckets, value)]++;
  }
  return expected;
}

// Creates the expected contents of an exponential histogram given parameters
// and values.
template <typename T>
std::vector<T> CreateExpectedExponentialHistogramContents(T floor, T initial_step,
                                                          T step_multiplier, size_t buckets,
                                                          const std::vector<T>& values) {
  const size_t underflow_bucket_offset = 3;
  const size_t array_size = 5 + buckets;
  std::vector<T> expected = {floor, initial_step, step_multiplier};
  expected.resize(array_size);
  for (T value : values) {
    expected[underflow_bucket_offset +
             ComputeExponentialBucketIndex(floor, initial_step, step_multiplier, buckets, value)]++;
  }
  return expected;
}

}  // namespace testing
}  // namespace inspect

#endif  // LIB_INSPECT_TESTING_CPP_INSPECT_H_
