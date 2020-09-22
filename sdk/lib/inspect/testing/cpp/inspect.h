// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_TESTING_CPP_INSPECT_H_
#define LIB_INSPECT_TESTING_CPP_INSPECT_H_

#include <lib/inspect/cpp/reader.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

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

// Matches a particular StringProperty with the given name using the given matcher.
::testing::Matcher<const PropertyValue&> StringIs(const std::string& name,
                                                  ::testing::Matcher<std::string> matcher);

// Matches a particular ByteVectorProperty with the given name using the given matcher.
::testing::Matcher<const PropertyValue&> ByteVectorIs(
    const std::string& name, ::testing::Matcher<std::vector<uint8_t>> matcher);

// Matches a particular IntProperty with the given name using the given matcher.
::testing::Matcher<const PropertyValue&> IntIs(const std::string& name,
                                               ::testing::Matcher<int64_t> matcher);

// Matches a particular UintProperty with the given name using the given matcher.
::testing::Matcher<const PropertyValue&> UintIs(const std::string& name,
                                                ::testing::Matcher<uint64_t> matcher);

// Matches a particular DoubleProperty with the given name using the given matcher.
::testing::Matcher<const PropertyValue&> DoubleIs(const std::string& name,
                                                  ::testing::Matcher<double> matcher);

// Matches a particular BoolProperty with the given name using the given matcher.
::testing::Matcher<const PropertyValue&> BoolIs(const std::string& name,
                                                ::testing::Matcher<bool> matcher);

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
// TODO(fxbug.dev/4582): Remove this.
::testing::Matcher<const Hierarchy&> ObjectMatches(NodeMatcher matcher);

// Matcher for the base path inside an Hierarchy.
::testing::Matcher<const Hierarchy&> PrefixPathMatches(PrefixPathMatcher matcher);

// Matcher for the children of the object in an Hierarchy.
::testing::Matcher<const Hierarchy&> ChildrenMatch(ChildrenMatcher matcher);

// Computes the bucket index for a value in a linear histogram.
// |total_buckets| is the number of buckets initially defined when the histogram
// property was created plus 2 (overflow and underflow).
template <typename T>
size_t ComputeLinearBucketIndex(T floor, T step_size, size_t total_buckets, T value) {
  size_t ret = 0;
  while (value >= floor && ret < total_buckets - 1) {
    floor += step_size;
    ret++;
  }
  return ret;
}

// Computes the bucket index for a value in an exponential histogram.
// |total_buckets| is the number of buckets initially defined when the histogram
// property was created plus 2 (overflow and underflow).
template <typename T>
size_t ComputeExponentialBucketIndex(T floor, T initial_step, T step_multiplier,
                                     size_t total_buckets, T value) {
  T current_floor = floor;
  T current_step = initial_step;
  size_t ret = 0;
  while (value >= current_floor && ret < total_buckets - 1) {
    current_floor = floor + current_step;
    current_step *= step_multiplier;
    ret++;
  }
  return ret;
}

// Creates the expected contents of a linear histogram given parameters and
// values.
template <typename T>
std::vector<T> CreateExpectedLinearHistogramContents(T floor, T step_size, size_t buckets,
                                                     const std::vector<T>& values) {
  // Linear Histogram arrays contain 2 fields with metadata.
  const size_t underflow_bucket_offset = 2;
  // |buckets| + 1 underflow bucket + 1 overflow bucket
  const size_t total_buckets = 2 + buckets;
  const size_t array_size = underflow_bucket_offset + total_buckets;
  std::vector<T> expected = {floor, step_size};
  expected.resize(array_size);
  for (T value : values) {
    expected[underflow_bucket_offset +
             ComputeLinearBucketIndex(floor, step_size, total_buckets, value)]++;
  }
  return expected;
}

// Creates the expected contents of an exponential histogram given parameters
// and values.
template <typename T>
std::vector<T> CreateExpectedExponentialHistogramContents(T floor, T initial_step,
                                                          T step_multiplier, size_t buckets,
                                                          const std::vector<T>& values) {
  // Exp. Histogram arrays contain 3 fields with metadata.
  const size_t underflow_bucket_offset = 3;
  // |buckets| + 1 underflow bucket + 1 overflow bucket
  const size_t total_buckets = 2 + buckets;
  const size_t array_size = underflow_bucket_offset + total_buckets;
  std::vector<T> expected = {floor, initial_step, step_multiplier};
  expected.resize(array_size);
  for (T value : values) {
    expected[underflow_bucket_offset + ComputeExponentialBucketIndex(floor, initial_step,
                                                                     step_multiplier, total_buckets,
                                                                     value)]++;
  }
  return expected;
}

}  // namespace testing
}  // namespace inspect

#endif  // LIB_INSPECT_TESTING_CPP_INSPECT_H_
