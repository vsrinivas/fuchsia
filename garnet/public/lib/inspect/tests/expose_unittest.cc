// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/inspect/deprecated/expose.h>
#include <lib/inspect/testing/inspect.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using component::Metric;
using component::Object;
using component::Property;
using testing::UnorderedElementsAre;

// These matchers are temporarily copied here to verify low-level operations on
// the FIDL Inspect API.
// TODO(crjohns): Delete this file when FIDL Inspect is removed.

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
    const std::string& name, const inspect::VectorValue& value) {
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

TEST(Property, StringValue) {
  Property a("test");

  EXPECT_THAT(a.ToFidl("key"), StringPropertyIs("key", "test"));
  a.Set("test2");
  EXPECT_THAT(a.ToFidl("key"), StringPropertyIs("key", "test2"));
}

TEST(Property, VectorValue) {
  Property::ByteVector test_vector;
  test_vector.push_back('\0');
  test_vector.push_back('\10');

  Property a(test_vector);

  EXPECT_THAT(a.ToFidl("key"), ByteVectorPropertyIs("key", test_vector));
  test_vector.push_back('a');
  a.Set(test_vector);
  EXPECT_THAT(a.ToFidl("key"), ByteVectorPropertyIs("key", test_vector));
}

TEST(Property, StringCallback) {
  Property a([] { return std::string("test"); });

  // Check callback is called.
  EXPECT_THAT(a.ToFidl("key"), StringPropertyIs("key", "test"));

  // Set to new callback, cancelling token. New value should be present.
  a.Set([] { return std::string("test2"); });
  EXPECT_THAT(a.ToFidl("key"), StringPropertyIs("key", "test2"));
}

TEST(Property, VectorCallback) {
  Property a([] { return Property::ByteVector(2, 'a'); });

  // Check callback is called.
  EXPECT_THAT(a.ToFidl("key"),
              ByteVectorPropertyIs("key", Property::ByteVector(2, 'a')));

  // Set to new callback, cancelling token. New value should be present.
  a.Set([] { return Property::ByteVector(2, 'b'); });
  EXPECT_THAT(a.ToFidl("key"),
              ByteVectorPropertyIs("key", Property::ByteVector(2, 'b')));
}

TEST(Metric, SetValue) {
  Metric a;

  EXPECT_THAT(a.ToFidl("key"), IntMetricIs("key", 0));

  a.SetInt(-10);
  EXPECT_THAT(a.ToFidl("key"), IntMetricIs("key", -10));

  a.SetUInt(1000);
  EXPECT_THAT(a.ToFidl("key"), UIntMetricIs("key", 1000));

  a.SetDouble(1.25);
  EXPECT_THAT(a.ToFidl("key"), DoubleMetricIs("key", 1.25));
}

TEST(Metric, Arithmetic) {
  Metric a;

  EXPECT_THAT(a.ToFidl("key"), IntMetricIs("key", 0));

  a.Sub(10);
  EXPECT_THAT(a.ToFidl("key"), IntMetricIs("key", -10));
  a.Sub(1.5);
  EXPECT_THAT(a.ToFidl("key"), IntMetricIs("key", -11));

  a.SetUInt(0);
  a.Add(1);
  EXPECT_THAT(a.ToFidl("key"), UIntMetricIs("key", 1));
  // Check that overflowing works properly.
  // Subtracting below 0 should wrap around.
  // Adding and subtracting by a double should also wrap.
  a.Sub(2);
  EXPECT_THAT(a.ToFidl("key"), UIntMetricIs("key", 0xFFFFFFFFFFFFFFFF));
  a.Add(2.12);
  EXPECT_THAT(a.ToFidl("key"), UIntMetricIs("key", 1));
  a.Sub(2.12);
  EXPECT_THAT(a.ToFidl("key"), UIntMetricIs("key", 0xFFFFFFFFFFFFFFFF));
  a.Add(-1);
  EXPECT_THAT(a.ToFidl("key"), UIntMetricIs("key", 0xFFFFFFFFFFFFFFFE));

  a.SetDouble(1.25);
  a.Add(0.5);
  EXPECT_THAT(a.ToFidl("key"), DoubleMetricIs("key", 1.75));
  a.Sub(1);
  EXPECT_THAT(a.ToFidl("key"), DoubleMetricIs("key", 0.75));
}

TEST(Metric, ValueCallback) {
  Metric a = component::CallbackMetric(
      [](Metric* out_metric) { out_metric->SetInt(10); });

  // Check callback is called.
  EXPECT_THAT(a.ToFidl("key"), IntMetricIs("key", 10));

  // Set to new callback, cancelling token. New value should be present.
  a.SetCallback([](Metric* out_metric) { out_metric->SetInt(11); });
  EXPECT_THAT(a.ToFidl("key"), IntMetricIs("key", 11));
}

TEST(Object, Name) {
  std::shared_ptr<Object> object = Object::Make("test");
  EXPECT_STREQ("test", object->name().c_str());
}

TEST(Object, ReadData) {
  std::shared_ptr<Object> object = Object::Make("test");
  object->SetProperty("property", component::Property("value"));
  object->SetMetric("int metric", component::IntMetric(-10));
  object->SetMetric("uint metric", component::UIntMetric(0xFF));
  object->SetMetric("double metric", component::DoubleMetric(0.25));

  fuchsia::inspect::Object obj;
  object->ReadData(
      [&obj](fuchsia::inspect::Object val) { obj = std::move(val); });

  EXPECT_THAT(obj.name, ::testing::Eq("test"));
  EXPECT_THAT(*obj.properties,
              UnorderedElementsAre(StringPropertyIs("property", "value")));
  EXPECT_THAT(*obj.metrics,
              UnorderedElementsAre(IntMetricIs("int metric", -10),
                                   UIntMetricIs("uint metric", 0xFF),
                                   DoubleMetricIs("double metric", 0.25)));
}

component::Object::StringOutputVector ListChildren(
    std::shared_ptr<Object> object) {
  component::Object::StringOutputVector ret;
  object->ListChildren([&ret](component::Object::StringOutputVector val) {
    ret = std::move(val);
  });
  return ret;
}

TEST(Object, SetTakeChild) {
  std::shared_ptr<Object> object = Object::Make("test");
  component::Object::StringOutputVector children_list;

  object->SetChild(Object::Make("child1"));
  children_list = ListChildren(object);
  EXPECT_THAT(*children_list, UnorderedElementsAre(fidl::StringPtr("child1")));

  auto child = object->TakeChild("child1");
  children_list = ListChildren(object);
  EXPECT_STREQ("child1", child->name().c_str());
  EXPECT_THAT(*children_list, UnorderedElementsAre());
}

TEST(Object, ChildrenCallback) {
  std::shared_ptr<Object> object = Object::Make("test");
  component::Object::StringOutputVector children_list;

  object->SetChild(Object::Make("concrete1"));
  object->SetChild(Object::Make("concrete2"));

  children_list = ListChildren(object);
  EXPECT_THAT(*children_list,
              UnorderedElementsAre(fidl::StringPtr("concrete1"),
                                   fidl::StringPtr("concrete2")));

  // Set the callback and ensure it is merged with the concrete objects.
  object->SetChildrenCallback([](component::Object::ObjectVector* out) {
    out->emplace_back(Object::Make("dynamic1"));
    out->emplace_back(Object::Make("dynamic2"));
    out->emplace_back(Object::Make("dynamic3"));
  });
  children_list = ListChildren(object);
  EXPECT_THAT(*children_list,
              UnorderedElementsAre(
                  fidl::StringPtr("concrete1"), fidl::StringPtr("concrete2"),
                  fidl::StringPtr("dynamic1"), fidl::StringPtr("dynamic2"),
                  fidl::StringPtr("dynamic3")));
}

}  // namespace
