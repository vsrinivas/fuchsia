// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspect.h"

namespace fuchsia {
namespace inspect {

void PrintTo(const ::fuchsia::inspect::Metric& metric, std::ostream* os) {
  if (metric.value.is_int_value())
    *os << "Int";
  if (metric.value.is_uint_value())
    *os << "UInt";
  if (metric.value.is_double_value())
    *os << "Double";
  *os << "Metric(" << ::testing::PrintToString(metric.key) << ", ";
  if (metric.value.is_int_value())
    *os << ::testing::PrintToString(metric.value.int_value());
  if (metric.value.is_uint_value())
    *os << ::testing::PrintToString(metric.value.uint_value());
  if (metric.value.is_double_value())
    *os << ::testing::PrintToString(metric.value.double_value());
  *os << ")";
}

void PrintTo(const ::fuchsia::inspect::Property& property, std::ostream* os) {
  if (property.value.is_str())
    *os << "String";
  if (property.value.is_bytes())
    *os << "ByteVector";
  *os << "Property(" << ::testing::PrintToString(property.key) << ", ";
  if (property.value.is_str())
    *os << ::testing::PrintToString(property.value.str());
  if (property.value.is_bytes())
    *os << ::testing::PrintToString(property.value.bytes());
  *os << ")";
}

void PrintTo(const ::fuchsia::inspect::Object& object, std::ostream* os) {
  *os << "Object(" << ::testing::PrintToString(object.name) << ", "
      << ::testing::PrintToString(object.metrics->size()) << " metrics, "
      << ::testing::PrintToString(object.properties->size()) << " properties)";
}

}  // namespace inspect
}  // namespace fuchsia

namespace inspect {
namespace testing {

internal::NameMatchesMatcher::NameMatchesMatcher(std::string name)
    : name_(std::move(name)) {}

bool internal::NameMatchesMatcher::MatchAndExplain(
    const ::fuchsia::inspect::Object& obj,
    ::testing::MatchResultListener* listener) const {
  if (obj.name != name_) {
    *listener << "expected name \"" << name_ << "\" but found \"" << obj.name
              << "\"";
    return false;
  }
  return true;
}

void internal::NameMatchesMatcher::DescribeTo(::std::ostream* os) const {
  *os << "name matches \"" << name_ << "\"";
}

void internal::NameMatchesMatcher::DescribeNegationTo(
    ::std::ostream* os) const {
  *os << "name does not match \"" << name_ << "\"";
}

internal::MetricListMatcher::MetricListMatcher(MetricsMatcher matcher)
    : matcher_(std::move(matcher)) {}

bool internal::MetricListMatcher::MatchAndExplain(
    const ::fuchsia::inspect::Object& obj,
    ::testing::MatchResultListener* listener) const {
  return ::testing::ExplainMatchResult(matcher_, *obj.metrics, listener);
}

void internal::MetricListMatcher::DescribeTo(::std::ostream* os) const {
  *os << "metric list ";
  matcher_.DescribeTo(os);
}

void internal::MetricListMatcher::DescribeNegationTo(::std::ostream* os) const {
  *os << "metric list ";
  matcher_.DescribeNegationTo(os);
}

internal::PropertyListMatcher::PropertyListMatcher(PropertiesMatcher matcher)
    : matcher_(std::move(matcher)) {}

bool internal::PropertyListMatcher::MatchAndExplain(
    const ::fuchsia::inspect::Object& obj,
    ::testing::MatchResultListener* listener) const {
  return ::testing::ExplainMatchResult(matcher_, *obj.properties, listener);
}

void internal::PropertyListMatcher::DescribeTo(::std::ostream* os) const {
  *os << "property list ";
  matcher_.DescribeTo(os);
}

void internal::PropertyListMatcher::DescribeNegationTo(
    ::std::ostream* os) const {
  *os << "property list ";
  matcher_.DescribeNegationTo(os);
}

::testing::Matcher<const fuchsia::inspect::Object&> NameMatches(
    std::string name) {
  return ::testing::MakeMatcher(
      new internal::NameMatchesMatcher(std::move(name)));
}

::testing::Matcher<const fuchsia::inspect::Object&> MetricList(
    MetricsMatcher matcher) {
  return ::testing::MakeMatcher(
      new internal::MetricListMatcher(std::move(matcher)));
}

::testing::Matcher<const fuchsia::inspect::Object&> PropertyList(
    PropertiesMatcher matcher) {
  return ::testing::MakeMatcher(
      new internal::PropertyListMatcher(std::move(matcher)));
}

::testing::Matcher<const fuchsia::inspect::Property&> StringPropertyIs(
    const std::string& name, const std::string& value) {
  return ::testing::AllOf(
      ::testing::Field(&fuchsia::inspect::Property::key,
                       ::testing::StrEq(name)),
      ::testing::Field(
          &fuchsia::inspect::Property::value,
          ::testing::Property(&fuchsia::inspect::PropertyValue::is_str,
                              ::testing::IsTrue())),
      ::testing::Field(
          &fuchsia::inspect::Property::value,
          ::testing::Property(&fuchsia::inspect::PropertyValue::str,
                              ::testing::StrEq(value))));
}

::testing::Matcher<const fuchsia::inspect::Property&> ByteVectorPropertyIs(
    const std::string& name, const VectorValue& value) {
  return ::testing::AllOf(
      ::testing::Field(&fuchsia::inspect::Property::key,
                       ::testing::StrEq(name)),
      ::testing::Field(
          &fuchsia::inspect::Property::value,
          ::testing::Property(&fuchsia::inspect::PropertyValue::is_bytes,
                              ::testing::IsTrue())),
      ::testing::Field(
          &fuchsia::inspect::Property::value,
          ::testing::Property(&fuchsia::inspect::PropertyValue::bytes,
                              ::testing::Eq(value))));
}

::testing::Matcher<const fuchsia::inspect::Metric&> IntMetricIs(
    const std::string& name, int64_t value) {
  return ::testing::AllOf(
      ::testing::Field(&fuchsia::inspect::Metric::key, ::testing::StrEq(name)),
      ::testing::Field(
          &fuchsia::inspect::Metric::value,
          ::testing::Property(&fuchsia::inspect::MetricValue::is_int_value,
                              ::testing::IsTrue())),
      ::testing::Field(
          &fuchsia::inspect::Metric::value,
          ::testing::Property(&fuchsia::inspect::MetricValue::int_value,
                              ::testing::Eq(value))));
}

::testing::Matcher<const fuchsia::inspect::Metric&> UIntMetricIs(
    const std::string& name, uint64_t value) {
  return ::testing::AllOf(
      ::testing::Field(&fuchsia::inspect::Metric::key, ::testing::StrEq(name)),
      ::testing::Field(
          &fuchsia::inspect::Metric::value,
          ::testing::Property(&fuchsia::inspect::MetricValue::is_uint_value,
                              ::testing::IsTrue())),
      ::testing::Field(
          &fuchsia::inspect::Metric::value,
          ::testing::Property(&fuchsia::inspect::MetricValue::uint_value,
                              ::testing::Eq(value))));
}

::testing::Matcher<const fuchsia::inspect::Metric&> DoubleMetricIs(
    const std::string& name, double value) {
  return ::testing::AllOf(
      ::testing::Field(&fuchsia::inspect::Metric::key, ::testing::StrEq(name)),
      ::testing::Field(
          &fuchsia::inspect::Metric::value,
          ::testing::Property(&fuchsia::inspect::MetricValue::is_double_value,
                              ::testing::IsTrue())),
      ::testing::Field(
          &fuchsia::inspect::Metric::value,
          ::testing::Property(&fuchsia::inspect::MetricValue::double_value,
                              ::testing::Eq(value))));
}

}  // namespace testing
}  // namespace inspect
