// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <random>
#include <vector>

#include <fbl/ref_ptr.h>
#include <fbl/string_printf.h>
#include <lib/fxl/strings/string_printf.h>
#include <perftest/perftest.h>
#include <zircon/syscalls.h>

#include <iostream>
#include <sstream>

#include "lib/component/cpp/exposed_object.h"

namespace {

using ByteVector = component::Property::ByteVector;
using component::ObjectPath;

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

  void increment() {
    object_dir().add_metric(path_, kValue, 1);
  }
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

// Measure the time taken to increment an IntMetric.
bool TestCreationAndDestruction(perftest::RepeatState* state) {
  state->DeclareStep("CreateMetric");
  state->DeclareStep("DestroyMetric");
  state->DeclareStep("CreateProperty");
  state->DeclareStep("DestroyProperty");
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
bool TestIncrement(perftest::RepeatState* state) {
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
bool TestString(perftest::RepeatState* state, int size) {
  PropertyItem item;
  std::string string;
  string.resize(size, 'a');
  while (state->KeepRunning()) {
    item.set(string);
  }
  return true;
}

// Measure the time taken to change a ByteVector property.
bool TestVector(perftest::RepeatState* state, int size) {
  PropertyItem item;
  ByteVector vector;
  vector.resize(size, 'a');
  while (state->KeepRunning()) {
    item.set(vector);
  }
  return true;
}

bool TestParentage(perftest::RepeatState* state) {
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

void RegisterTests() {
  perftest::RegisterTest("Inspect/CreateDestroy", TestCreationAndDestruction);
  perftest::RegisterTest("Inspect/Increment", TestIncrement);
  perftest::RegisterTest("Inspect/Parentage", TestParentage);
  perftest::RegisterTest("Inspect/Path0", TestIncrementPath, kPath0);
  perftest::RegisterTest("Inspect/Path1", TestIncrementPath, kPath1);
  perftest::RegisterTest("Inspect/Path2", TestIncrementPath, kPath2);
  perftest::RegisterTest("Inspect/Path10", TestIncrementPath, kPath10);
  perftest::RegisterTest(fxl::StringPrintf("Inspect/String%d", kSmallPropertySize).c_str(),
                         TestString, kSmallPropertySize);
  perftest::RegisterTest(fxl::StringPrintf("Inspect/String%d", kLargePropertySize).c_str(),
                         TestString, kLargePropertySize);
  perftest::RegisterTest(fxl::StringPrintf("Inspect/Vector%d", kSmallPropertySize).c_str(),
                         TestVector, kSmallPropertySize);
  perftest::RegisterTest(fxl::StringPrintf("Inspect/Vector%d", kLargePropertySize).c_str(),
                         TestVector, kLargePropertySize);
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
