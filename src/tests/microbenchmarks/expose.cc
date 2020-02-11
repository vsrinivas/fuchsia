// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include <iostream>
#include <random>
#include <sstream>
#include <vector>

#include <fbl/ref_ptr.h>
#include <fbl/string_printf.h>
#include <perftest/perftest.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/inspect_deprecated/deprecated/exposed_object.h"

namespace {

using ByteVector = component::Property::ByteVector;
using component::CallbackMetric;
using component::DoubleMetric;
using component::IntMetric;
using component::Metric;
using component::Object;
using component::ObjectPath;
using component::Property;
using component::UIntMetric;

const char kValue[] = "value";
const int kSmallPropertySize = 8;
const int kLargePropertySize = 10000;
const ObjectPath kPath0 = {};
const ObjectPath kPath1 = {"a"};
const ObjectPath kPath2 = {"a", "b"};
const ObjectPath kPath10 = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"};

class NumericItem : public component::ExposedObject {
 public:
  NumericItem(ObjectPath path) : ExposedObject(UniqueName("itemN-")), path_{std::move(path)} {
    object_dir().set_metric(path_, kValue, component::IntMetric(0));
  }
  NumericItem() : NumericItem(ObjectPath()) {}

  void increment() { object_dir().add_metric(path_, kValue, 1); }

 private:
  ObjectPath path_;
};

class PropertyItem : public component::ExposedObject {
 public:
  PropertyItem() : ExposedObject(UniqueName("itemS-")) {
    object_dir().set_prop(kValue, component::Property());
  }
  void set(std::string str_value) { object_dir().set_prop(kValue, std::move(str_value)); }
  void set(ByteVector vector_value) { object_dir().set_prop(kValue, std::move(vector_value)); }
};

// Measure the time taken to create and destroy metrics and properties.
bool TestExposedObjectLifecycle(perftest::RepeatState* state) {
  state->DeclareStep("MetricCreate");
  state->DeclareStep("MetricDestroy");
  state->DeclareStep("PropertyCreate");
  state->DeclareStep("PropertyDestroy");
  while (state->KeepRunning()) {
    {
      NumericItem item;
      state->NextStep();
    }
    state->NextStep();
    {
      PropertyItem item;
      state->NextStep();
    }
  }
  return true;
}

// Measure the time taken to increment an IntMetric.
bool TestExposedObjectIncrement(perftest::RepeatState* state) {
  NumericItem item;
  while (state->KeepRunning()) {
    item.increment();
  }
  return true;
}

// Measure the time taken to increment an IntMetric, given a path.
bool TestIncrementPath(perftest::RepeatState* state, ObjectPath path) {
  NumericItem item(path);
  while (state->KeepRunning()) {
    item.increment();
  }
  return true;
}

// Measure the time taken to change a String property.
bool TestExposedObjectSetString(perftest::RepeatState* state, int size) {
  PropertyItem item;
  std::string string;
  string.resize(size, 'a');
  while (state->KeepRunning()) {
    item.set(string);
  }
  return true;
}

// Measure the time taken to change a ByteVector property.
bool TestExposedObjectSetVector(perftest::RepeatState* state, int size) {
  PropertyItem item;
  ByteVector vector;
  vector.resize(size, 'a');
  while (state->KeepRunning()) {
    item.set(vector);
  }
  return true;
}

bool TestExposedObjectParenting(perftest::RepeatState* state) {
  NumericItem parent;
  NumericItem child1;
  NumericItem child2;
  NumericItem child3;
  state->DeclareStep("AddFirst");
  state->DeclareStep("AddSecond");
  state->DeclareStep("AddFirstAgain");
  state->DeclareStep("AddThird");
  state->DeclareStep("RemoveFirst");
  state->DeclareStep("RemoveSecond");
  state->DeclareStep("RemoveFirstAgain");
  state->DeclareStep("RemoveThird");
  while (state->KeepRunning()) {
    child1.set_parent(parent.object_dir());
    state->NextStep();
    child2.set_parent(parent.object_dir());
    state->NextStep();
    child1.set_parent(parent.object_dir());
    state->NextStep();
    child3.set_parent(parent.object_dir());
    state->NextStep();
    child1.remove_from_parent();
    state->NextStep();
    child2.remove_from_parent();
    state->NextStep();
    child1.remove_from_parent();
    state->NextStep();
    child3.remove_from_parent();
  }
  return true;
}

bool TestIntMetricLifecycle(perftest::RepeatState* state) {
  state->DeclareStep("Create");
  state->DeclareStep("Destroy");
  while (state->KeepRunning()) {
    Metric item = IntMetric(5);
    state->NextStep();
  }
  return true;
}

bool TestIntMetricSet(perftest::RepeatState* state) {
  Metric item = IntMetric(5);
  while (state->KeepRunning()) {
    item.SetInt(5);
  }
  return true;
}

bool TestIntMetricAdd(perftest::RepeatState* state) {
  Metric item = IntMetric(5);
  while (state->KeepRunning()) {
    item.Add(5);
  }
  return true;
}

bool TestIntMetricSub(perftest::RepeatState* state) {
  Metric item = IntMetric(5);
  while (state->KeepRunning()) {
    item.Sub(5);
  }
  return true;
}

bool TestIntMetricToString(perftest::RepeatState* state) {
  Metric item = IntMetric(5);
  while (state->KeepRunning()) {
    perftest::DoNotOptimize(item.ToString());
  }
  return true;
}

bool TestIntMetricToFidl(perftest::RepeatState* state) {
  Metric item = IntMetric(5);
  while (state->KeepRunning()) {
    perftest::DoNotOptimize(item.ToFidl("a_name"));
  }
  return true;
}

bool TestUIntMetricLifecycle(perftest::RepeatState* state) {
  state->DeclareStep("Create");
  state->DeclareStep("Destroy");
  while (state->KeepRunning()) {
    Metric item = UIntMetric(5);
    state->NextStep();
  }
  return true;
}

bool TestUIntMetricSet(perftest::RepeatState* state) {
  Metric item = UIntMetric(5);
  while (state->KeepRunning()) {
    item.SetInt(5);
  }
  return true;
}

bool TestUIntMetricAdd(perftest::RepeatState* state) {
  Metric item = UIntMetric(5);
  while (state->KeepRunning()) {
    item.Add(5);
  }
  return true;
}

bool TestUIntMetricSub(perftest::RepeatState* state) {
  Metric item = UIntMetric(5);
  while (state->KeepRunning()) {
    item.Sub(5);
  }
  return true;
}

bool TestUIntMetricToString(perftest::RepeatState* state) {
  Metric item = UIntMetric(5);
  while (state->KeepRunning()) {
    perftest::DoNotOptimize(item.ToString());
  }
  return true;
}

bool TestUIntMetricToFidl(perftest::RepeatState* state) {
  Metric item = UIntMetric(5);
  while (state->KeepRunning()) {
    perftest::DoNotOptimize(item.ToFidl("a_name"));
  }
  return true;
}

bool TestDoubleMetricLifecycle(perftest::RepeatState* state) {
  state->DeclareStep("Create");
  state->DeclareStep("Destroy");
  while (state->KeepRunning()) {
    Metric item = DoubleMetric(5);
    state->NextStep();
  }
  return true;
}

bool TestDoubleMetricSet(perftest::RepeatState* state) {
  Metric item = DoubleMetric(5);
  while (state->KeepRunning()) {
    item.SetDouble(5);
  }
  return true;
}

bool TestDoubleMetricAdd(perftest::RepeatState* state) {
  Metric item = DoubleMetric(5);
  while (state->KeepRunning()) {
    item.Add(5);
  }
  return true;
}

bool TestDoubleMetricSub(perftest::RepeatState* state) {
  Metric item = DoubleMetric(5);
  while (state->KeepRunning()) {
    item.Sub(5);
  }
  return true;
}

bool TestDoubleMetricToString(perftest::RepeatState* state) {
  Metric item = DoubleMetric(5);
  while (state->KeepRunning()) {
    perftest::DoNotOptimize(item.ToString());
  }
  return true;
}

bool TestDoubleMetricToFidl(perftest::RepeatState* state) {
  Metric item = DoubleMetric(5);
  while (state->KeepRunning()) {
    perftest::DoNotOptimize(item.ToFidl("a_name"));
  }
  return true;
}

bool TestCallbackMetricLifecycle(perftest::RepeatState* state) {
  state->DeclareStep("Create");
  state->DeclareStep("Destroy");
  while (state->KeepRunning()) {
    Metric item = CallbackMetric([](Metric* out_metric) { out_metric->SetInt(10); });
    state->NextStep();
  }
  return true;
}

bool TestCallbackMetricSet(perftest::RepeatState* state) {
  Metric item = CallbackMetric([](Metric* out_metric) { out_metric->SetInt(10); });
  while (state->KeepRunning()) {
    item.SetCallback([](Metric* out_metric) { out_metric->SetInt(10); });
  }
  return true;
}

bool TestCallbackMetricToString(perftest::RepeatState* state) {
  Metric item = CallbackMetric([](Metric* out_metric) { out_metric->SetInt(10); });
  while (state->KeepRunning()) {
    perftest::DoNotOptimize(item.ToString());
  }
  return true;
}

bool TestCallbackMetricToFidl(perftest::RepeatState* state) {
  Metric item = CallbackMetric([](Metric* out_metric) { out_metric->SetInt(10); });
  while (state->KeepRunning()) {
    perftest::DoNotOptimize(item.ToFidl("a_name"));
  }
  return true;
}

bool TestStringPropertyLifecycle(perftest::RepeatState* state, int size) {
  std::string data;
  data.resize(size, 'a');
  state->DeclareStep("Create");
  state->DeclareStep("Destroy");
  while (state->KeepRunning()) {
    Property item(data);
    state->NextStep();
  }
  return true;
}

bool TestStringPropertySet(perftest::RepeatState* state, int size) {
  std::string data;
  data.resize(size, 'a');
  Property item(data);
  while (state->KeepRunning()) {
    item.Set(data);
  }
  return true;
}

bool TestStringPropertyToFidl(perftest::RepeatState* state, int size) {
  std::string data;
  data.resize(size, 'a');
  Property item(data);
  while (state->KeepRunning()) {
    perftest::DoNotOptimize(item.ToFidl("a_name"));
  }
  return true;
}

bool TestVectorPropertyLifecycle(perftest::RepeatState* state, int size) {
  ByteVector data;
  data.resize(size, 'a');
  state->DeclareStep("Create");
  state->DeclareStep("Destroy");
  while (state->KeepRunning()) {
    Property item(data);
    state->NextStep();
  }
  return true;
}

bool TestVectorPropertySet(perftest::RepeatState* state, int size) {
  ByteVector data;
  data.resize(size, 'a');
  Property item(data);
  while (state->KeepRunning()) {
    item.Set(data);
  }
  return true;
}

bool TestVectorPropertyToFidl(perftest::RepeatState* state, int size) {
  ByteVector data;
  data.resize(size, 'a');
  Property item(data);
  while (state->KeepRunning()) {
    perftest::DoNotOptimize(item.ToFidl("a_name"));
  }
  return true;
}

bool TestCallbackPropertyLifecycle(perftest::RepeatState* state) {
  state->DeclareStep("Create");
  state->DeclareStep("Destroy");
  while (state->KeepRunning()) {
    Property item = Property([]() { return "a"; });
    state->NextStep();
  }
  return true;
}

bool TestCallbackPropertySet(perftest::RepeatState* state) {
  Property item = Property([]() { return "a"; });
  while (state->KeepRunning()) {
    item.Set([]() { return "a"; });
  }
  return true;
}

bool TestObjectLifecycle(perftest::RepeatState* state) {
  state->DeclareStep("Create");
  state->DeclareStep("Destroy");
  while (state->KeepRunning()) {
    auto item = Object::Make("a_name");
    state->NextStep();
  }
  return true;
}

bool TestObjectParenting(perftest::RepeatState* state) {
  auto parent = Object::Make("parent");
  auto first_child = Object::Make("first");
  auto second_child = Object::Make("second");
  state->DeclareStep("AllocateChildren");
  state->DeclareStep("AddFirstChild");
  state->DeclareStep("AddSecondChild");
  state->DeclareStep("GetFirstChild");
  state->DeclareStep("GetInvalidChild");
  state->DeclareStep("RemoveSecondChild");
  state->DeclareStep("RemoveFirstChild");
  state->DeclareStep("RemoveInvalidChild");
  while (state->KeepRunning()) {
    state->NextStep();
    parent->SetChild(first_child);
    state->NextStep();
    parent->SetChild(second_child);
    state->NextStep();
    perftest::DoNotOptimize(parent->GetChild("first"));
    state->NextStep();
    perftest::DoNotOptimize(parent->GetChild("invalid"));
    state->NextStep();
    parent->TakeChild("second");
    state->NextStep();
    parent->TakeChild("first");
    state->NextStep();
    parent->TakeChild("invalid");
  }
  return true;
}

bool TestObjectMetricOperations(perftest::RepeatState* state) {
  auto parent = Object::Make("parent");
  state->DeclareStep("CreateMetric");
  state->DeclareStep("Set");
  state->DeclareStep("Add");
  state->DeclareStep("Sub");
  state->DeclareStep("Remove");
  state->DeclareStep("RemoveMissing");
  while (state->KeepRunning()) {
    Metric metric = IntMetric(10);
    state->NextStep();
    parent->SetMetric("metric", std::move(metric));
    state->NextStep();
    parent->AddMetric("metric", 1);
    state->NextStep();
    parent->SubMetric("metric", 1);
    state->NextStep();
    parent->RemoveMetric("metric");
    state->NextStep();
    parent->RemoveMetric("not_there");
  }
  return true;
}

bool TestObjectPropertyOperations(perftest::RepeatState* state) {
  auto parent = Object::Make("parent");
  state->DeclareStep("CreateProperty");
  state->DeclareStep("Set");
  state->DeclareStep("Remove");
  state->DeclareStep("RemoveMissing");
  while (state->KeepRunning()) {
    Property property = Property("data");
    state->NextStep();
    parent->SetProperty("property", std::move(property));
    state->NextStep();
    parent->RemoveMetric("property");
    state->NextStep();
    parent->RemoveMetric("not_there");
  }
  return true;
}

bool TestObjectChildrenCallback(perftest::RepeatState* state) {
  auto parent = Object::Make("parent");
  state->DeclareStep("SetCallback");
  state->DeclareStep("RemoveCallback");
  while (state->KeepRunning()) {
    parent->SetChildrenCallback([](auto vector) {});
    state->NextStep();
    parent->ClearChildrenCallback();
  }
  // TODO(maybe): Test non-empty objects?
  return true;
}

bool TestObjectToFidl(perftest::RepeatState* state) {
  auto parent = Object::Make("parent");
  while (state->KeepRunning()) {
    perftest::DoNotOptimize(parent->ToFidl());
  }
  // TODO(maybe): Test non-empty objects?
  return true;
}

void RegisterTests() {
  perftest::RegisterTest("Expose/ExposedObject/Lifecycle", TestExposedObjectLifecycle);
  perftest::RegisterTest("Expose/ExposedObject/Increment", TestExposedObjectIncrement);
  perftest::RegisterTest("Expose/ExposedObject/Parenting", TestExposedObjectParenting);
  perftest::RegisterTest("Expose/ExposedObject/Path/0", TestIncrementPath, kPath0);
  perftest::RegisterTest("Expose/ExposedObject/Path/1", TestIncrementPath, kPath1);
  perftest::RegisterTest("Expose/ExposedObject/Path/2", TestIncrementPath, kPath2);
  perftest::RegisterTest("Expose/ExposedObject/Path/10", TestIncrementPath, kPath10);
  perftest::RegisterTest(
      fxl::StringPrintf("Expose/ExposedObject/SetString/%d", kSmallPropertySize).c_str(),
      TestExposedObjectSetString, kSmallPropertySize);
  perftest::RegisterTest(
      fxl::StringPrintf("Expose/ExposedObject/SetString/%d", kLargePropertySize).c_str(),
      TestExposedObjectSetString, kLargePropertySize);
  perftest::RegisterTest(
      fxl::StringPrintf("Expose/ExposedObject/SetVector/%d", kSmallPropertySize).c_str(),
      TestExposedObjectSetVector, kSmallPropertySize);
  perftest::RegisterTest(
      fxl::StringPrintf("Expose/ExposedObject/SetVector/%d", kLargePropertySize).c_str(),
      TestExposedObjectSetVector, kLargePropertySize);

  perftest::RegisterTest("Expose/IntMetric/Lifecycle", TestIntMetricLifecycle);
  perftest::RegisterTest("Expose/IntMetric/Set", TestIntMetricSet);
  perftest::RegisterTest("Expose/IntMetric/Add", TestIntMetricAdd);
  perftest::RegisterTest("Expose/IntMetric/Sub", TestIntMetricSub);
  perftest::RegisterTest("Expose/IntMetric/ToString", TestIntMetricToString);
  perftest::RegisterTest("Expose/IntMetric/ToFidl", TestIntMetricToFidl);

  perftest::RegisterTest("Expose/UIntMetric/Lifecycle", TestUIntMetricLifecycle);
  perftest::RegisterTest("Expose/UIntMetric/Set", TestUIntMetricSet);
  perftest::RegisterTest("Expose/UIntMetric/Add", TestUIntMetricAdd);
  perftest::RegisterTest("Expose/UIntMetric/Sub", TestUIntMetricSub);
  perftest::RegisterTest("Expose/UIntMetric/ToString", TestUIntMetricToString);
  perftest::RegisterTest("Expose/UIntMetric/ToFidl", TestUIntMetricToFidl);

  perftest::RegisterTest("Expose/DoubleMetric/Lifecycle", TestDoubleMetricLifecycle);
  perftest::RegisterTest("Expose/DoubleMetric/Set", TestDoubleMetricSet);
  perftest::RegisterTest("Expose/DoubleMetric/Add", TestDoubleMetricAdd);
  perftest::RegisterTest("Expose/DoubleMetric/Sub", TestDoubleMetricSub);
  perftest::RegisterTest("Expose/DoubleMetric/ToString", TestDoubleMetricToString);
  perftest::RegisterTest("Expose/DoubleMetric/ToFidl", TestDoubleMetricToFidl);

  perftest::RegisterTest("Expose/CallbackMetric/Lifecycle", TestCallbackMetricLifecycle);
  perftest::RegisterTest("Expose/CallbackMetric/Set", TestCallbackMetricSet);
  perftest::RegisterTest("Expose/CallbackMetric/ToString", TestCallbackMetricToString);
  perftest::RegisterTest("Expose/CallbackMetric/ToFidl", TestCallbackMetricToFidl);

  perftest::RegisterTest(
      fxl::StringPrintf("Expose/StringProperty/Lifecycle/%d", kSmallPropertySize).c_str(),
      TestStringPropertyLifecycle, kSmallPropertySize);
  perftest::RegisterTest(
      fxl::StringPrintf("Expose/StringProperty/Set/%d", kSmallPropertySize).c_str(),
      TestStringPropertySet, kSmallPropertySize);
  perftest::RegisterTest(
      fxl::StringPrintf("Expose/StringProperty/ToFidl/%d", kSmallPropertySize).c_str(),
      TestStringPropertyToFidl, kSmallPropertySize);

  perftest::RegisterTest(
      fxl::StringPrintf("Expose/StringProperty/Lifecycle/%d", kLargePropertySize).c_str(),
      TestStringPropertyLifecycle, kLargePropertySize);
  perftest::RegisterTest(
      fxl::StringPrintf("Expose/StringProperty/Set/%d", kLargePropertySize).c_str(),
      TestStringPropertySet, kLargePropertySize);
  perftest::RegisterTest(
      fxl::StringPrintf("Expose/StringProperty/ToFidl/%d", kLargePropertySize).c_str(),
      TestStringPropertyToFidl, kLargePropertySize);

  perftest::RegisterTest(
      fxl::StringPrintf("Expose/VectorProperty/Lifecycle/%d", kSmallPropertySize).c_str(),
      TestVectorPropertyLifecycle, kSmallPropertySize);
  perftest::RegisterTest(
      fxl::StringPrintf("Expose/VectorProperty/Set/%d", kSmallPropertySize).c_str(),
      TestVectorPropertySet, kSmallPropertySize);
  perftest::RegisterTest(
      fxl::StringPrintf("Expose/VectorProperty/ToFidl/%d", kSmallPropertySize).c_str(),
      TestVectorPropertyToFidl, kSmallPropertySize);

  perftest::RegisterTest(
      fxl::StringPrintf("Expose/VectorProperty/Lifecycle/%d", kLargePropertySize).c_str(),
      TestVectorPropertyLifecycle, kLargePropertySize);
  perftest::RegisterTest(
      fxl::StringPrintf("Expose/VectorProperty/Set/%d", kLargePropertySize).c_str(),
      TestVectorPropertySet, kLargePropertySize);
  perftest::RegisterTest(
      fxl::StringPrintf("Expose/VectorProperty/ToFidl/%d", kLargePropertySize).c_str(),
      TestVectorPropertyToFidl, kLargePropertySize);

  perftest::RegisterTest("Expose/CallbackProperty/Lifecycle", TestCallbackPropertyLifecycle);
  perftest::RegisterTest("Expose/CallbackProperty/Set", TestCallbackPropertySet);

  perftest::RegisterTest("Expose/Object/Lifecycle", TestObjectLifecycle);
  perftest::RegisterTest("Expose/Object/Parenting", TestObjectParenting);
  perftest::RegisterTest("Expose/Object/MetricOperations", TestObjectMetricOperations);
  perftest::RegisterTest("Expose/Object/PropertyOperations", TestObjectPropertyOperations);
  perftest::RegisterTest("Expose/Object/ToFidl", TestObjectToFidl);
  perftest::RegisterTest("Expose/Object/ChildrenCallback", TestObjectChildrenCallback);
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
