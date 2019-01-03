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

const char kValue[] = "value";
const int kSmallPropertySize = 8;
const int kLargePropertySize = 10000;

class NumericItem : public component::ExposedObject {
 public:
  NumericItem() : ExposedObject(UniqueName("itemN-")) {
    object_dir().set_metric(kValue, component::IntMetric(0));
  }
  void increment() { object_dir().add_metric(kValue, 1); }
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
bool TestIncrement(perftest::RepeatState* state) {
  NumericItem item;
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
  PropertyItem* item = new PropertyItem();
  ByteVector vector;
  vector.resize(size, 'a');
  ZX_ASSERT(item != nullptr);
  while (state->KeepRunning()) {
    item->set(vector);
  }
  return true;
}

void RegisterTests() {
  perftest::RegisterTest("Inspect/Increment", TestIncrement);
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
