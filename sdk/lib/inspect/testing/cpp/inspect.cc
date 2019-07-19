// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sdk/lib/inspect/testing/cpp/inspect.h>

using inspect::NodeValue;
using inspect::PropertyValue;

namespace {}  // namespace

namespace inspect {

void PrintTo(const PropertyValue& property, std::ostream* os) {
  if (property.format() == PropertyFormat::INT)
    *os << "Int";
  if (property.format() == PropertyFormat::UINT)
    *os << "Uint";
  if (property.format() == PropertyFormat::DOUBLE)
    *os << "Double";
  if (property.format() == PropertyFormat::INT_ARRAY)
    *os << "IntArray";
  if (property.format() == PropertyFormat::UINT_ARRAY)
    *os << "UintArray";
  if (property.format() == PropertyFormat::DOUBLE_ARRAY)
    *os << "DoubleArray";
  if (property.format() == PropertyFormat::STRING)
    *os << "String";
  if (property.format() == PropertyFormat::BYTES)
    *os << "ByteVector";
  *os << "Property(" << ::testing::PrintToString(property.name()) << ", ";
  if (property.format() == PropertyFormat::INT)
    *os << ::testing::PrintToString(property.Get<IntPropertyValue>().value());
  if (property.format() == PropertyFormat::UINT)
    *os << ::testing::PrintToString(property.Get<UintPropertyValue>().value());
  if (property.format() == PropertyFormat::DOUBLE)
    *os << ::testing::PrintToString(property.Get<DoublePropertyValue>().value());
  if (property.format() == PropertyFormat::INT_ARRAY)
    *os << ::testing::PrintToString(property.Get<IntArrayValue>().value());
  if (property.format() == PropertyFormat::UINT_ARRAY)
    *os << ::testing::PrintToString(property.Get<UintArrayValue>().value());
  if (property.format() == PropertyFormat::DOUBLE_ARRAY)
    *os << ::testing::PrintToString(property.Get<DoubleArrayValue>().value());
  if (property.format() == PropertyFormat::STRING)
    *os << ::testing::PrintToString(property.Get<StringPropertyValue>().value());
  if (property.format() == PropertyFormat::BYTES)
    *os << ::testing::PrintToString(property.Get<ByteVectorPropertyValue>().value());
  *os << ")";
}

void PrintTo(const NodeValue& node, std::ostream* os) {
  *os << "Node(" << ::testing::PrintToString(node.name()) << ", "
      << ::testing::PrintToString(node.properties().size()) << " properties)";
}

void PrintTo(const ::inspect::Hierarchy& hierarchy, std::ostream* os) {
  *os << "Hierarchy(" << ::testing::PrintToString(hierarchy.node()) << ", "
      << ::testing::PrintToString(hierarchy.children().size()) << " children)";
}

namespace testing {

internal::NameMatchesMatcher::NameMatchesMatcher(std::string name) : name_(std::move(name)) {}

bool internal::NameMatchesMatcher::MatchAndExplain(const NodeValue& node,
                                                   ::testing::MatchResultListener* listener) const {
  if (node.name() != name_) {
    *listener << "expected name \"" << name_ << "\" but found \"" << node.name() << "\"";
    return false;
  }
  return true;
}

void internal::NameMatchesMatcher::DescribeTo(::std::ostream* os) const {
  *os << "name matches \"" << name_ << "\"";
}

void internal::NameMatchesMatcher::DescribeNegationTo(::std::ostream* os) const {
  *os << "name does not match \"" << name_ << "\"";
}

internal::PropertyListMatcher::PropertyListMatcher(PropertiesMatcher matcher)
    : matcher_(std::move(matcher)) {}

bool internal::PropertyListMatcher::MatchAndExplain(
    const NodeValue& node, ::testing::MatchResultListener* listener) const {
  return ::testing::ExplainMatchResult(matcher_, node.properties(), listener);
}

void internal::PropertyListMatcher::DescribeTo(::std::ostream* os) const {
  *os << "property list ";
  matcher_.DescribeTo(os);
}

void internal::PropertyListMatcher::DescribeNegationTo(::std::ostream* os) const {
  *os << "property list ";
  matcher_.DescribeNegationTo(os);
}

::testing::Matcher<const NodeValue&> NameMatches(std::string name) {
  return ::testing::MakeMatcher(new internal::NameMatchesMatcher(std::move(name)));
}

::testing::Matcher<const NodeValue&> PropertyList(PropertiesMatcher matcher) {
  return ::testing::MakeMatcher(new internal::PropertyListMatcher(std::move(matcher)));
}

::testing::Matcher<const PropertyValue&> StringIs(const std::string& name,
                                                  const std::string& value) {
  return ::testing::AllOf(::testing::Property(&PropertyValue::name, ::testing::StrEq(name)),
                          ::testing::Property(&PropertyValue::format, PropertyFormat::STRING),
                          ::testing::Property(&PropertyValue::Get<StringPropertyValue>,
                                              ::testing::Property(&StringPropertyValue::value,
                                                                  ::testing::StrEq(value))));
}

::testing::Matcher<const PropertyValue&> ByteVectorIs(const std::string& name,
                                                      const std::vector<uint8_t>& value) {
  return ::testing::AllOf(::testing::Property(&PropertyValue::name, ::testing::StrEq(name)),
                          ::testing::Property(&PropertyValue::format, PropertyFormat::BYTES),
                          ::testing::Property(&PropertyValue::Get<ByteVectorPropertyValue>,
                                              ::testing::Property(&ByteVectorPropertyValue::value,
                                                                  ::testing::Eq(value))));
}

::testing::Matcher<const PropertyValue&> IntIs(const std::string& name, int64_t value) {
  return ::testing::AllOf(
      ::testing::Property(&PropertyValue::name, ::testing::StrEq(name)),
      ::testing::Property(&PropertyValue::format, PropertyFormat::INT),
      ::testing::Property(&PropertyValue::Get<IntPropertyValue>,
                          ::testing::Property(&IntPropertyValue::value, ::testing::Eq(value))));
}

::testing::Matcher<const PropertyValue&> UintIs(const std::string& name, uint64_t value) {
  return ::testing::AllOf(
      ::testing::Property(&PropertyValue::name, ::testing::StrEq(name)),
      ::testing::Property(&PropertyValue::format, PropertyFormat::UINT),
      ::testing::Property(&PropertyValue::Get<UintPropertyValue>,
                          ::testing::Property(&UintPropertyValue::value, ::testing::Eq(value))));
}

::testing::Matcher<const PropertyValue&> DoubleIs(const std::string& name, double value) {
  return ::testing::AllOf(
      ::testing::Property(&PropertyValue::name, ::testing::StrEq(name)),
      ::testing::Property(&PropertyValue::format, PropertyFormat::DOUBLE),
      ::testing::Property(&PropertyValue::Get<DoublePropertyValue>,
                          ::testing::Property(&DoublePropertyValue::value, ::testing::Eq(value))));
}

::testing::Matcher<const PropertyValue&> IntArrayIs(
    const std::string& name, ::testing::Matcher<std::vector<int64_t>> matcher) {
  return ::testing::AllOf(
      ::testing::Property(&PropertyValue::name, ::testing::StrEq(name)),
      ::testing::Property(&PropertyValue::format, PropertyFormat::INT_ARRAY),
      ::testing::Property(&PropertyValue::Get<IntArrayValue>,
                          ::testing::Property(&IntArrayValue::value, std::move(matcher))));
}

::testing::Matcher<const PropertyValue&> UintArrayIs(
    const std::string& name, ::testing::Matcher<std::vector<uint64_t>> matcher) {
  return ::testing::AllOf(
      ::testing::Property(&PropertyValue::name, ::testing::StrEq(name)),
      ::testing::Property(&PropertyValue::format, PropertyFormat::UINT_ARRAY),
      ::testing::Property(&PropertyValue::Get<UintArrayValue>,
                          ::testing::Property(&UintArrayValue::value, std::move(matcher))));
}

::testing::Matcher<const PropertyValue&> DoubleArrayIs(
    const std::string& name, ::testing::Matcher<std::vector<double>> matcher) {
  return ::testing::AllOf(
      ::testing::Property(&PropertyValue::name, ::testing::StrEq(name)),
      ::testing::Property(&PropertyValue::format, PropertyFormat::DOUBLE_ARRAY),
      ::testing::Property(&PropertyValue::Get<DoubleArrayValue>,
                          ::testing::Property(&DoubleArrayValue::value, std::move(matcher))));
}

::testing::Matcher<const PropertyValue&> ArrayDisplayFormatIs(ArrayDisplayFormat format) {
  return ::testing::AnyOf(
      ::testing::AllOf(
          ::testing::Property(&PropertyValue::format, PropertyFormat::INT_ARRAY),
          ::testing::Property(&PropertyValue::Get<IntArrayValue>,
                              ::testing::Property(&IntArrayValue::GetDisplayFormat, format))),
      ::testing::AllOf(
          ::testing::Property(&PropertyValue::format, PropertyFormat::UINT_ARRAY),
          ::testing::Property(&PropertyValue::Get<UintArrayValue>,
                              ::testing::Property(&UintArrayValue::GetDisplayFormat, format))),
      ::testing::AllOf(
          ::testing::Property(&PropertyValue::format, PropertyFormat::DOUBLE_ARRAY),
          ::testing::Property(&PropertyValue::Get<DoubleArrayValue>,
                              ::testing::Property(&DoubleArrayValue::GetDisplayFormat, format))));
}

::testing::Matcher<const Hierarchy&> NodeMatches(NodeMatcher matcher) {
  return ::testing::Property(&Hierarchy::node, std::move(matcher));
}

::testing::Matcher<const Hierarchy&> ObjectMatches(NodeMatcher matcher) {
  return NodeMatches(std::move(matcher));
}

::testing::Matcher<const Hierarchy&> ChildrenMatch(ChildrenMatcher matcher) {
  return ::testing::Property(&Hierarchy::children, std::move(matcher));
}

}  // namespace testing
}  // namespace inspect
