// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/ref_ptr.h>
#include <fbl/string_printf.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <perftest/perftest.h>
#include <zircon/syscalls.h>

#include <iostream>
#include <sstream>

#include "lib/inspect-vmo/inspect.h"

namespace {

const char* kName = "name";
using inspect::vmo::Inspector;
using inspect::vmo::Object;
using inspect::vmo::internal::NumericMetric;

template <typename T>
NumericMetric<T> CreateMetric(Object* root);

template <>
NumericMetric<int64_t> CreateMetric<int64_t>(Object* root) {
  return root->CreateIntMetric(kName, 0);
}

template <>
NumericMetric<uint64_t> CreateMetric<uint64_t>(Object* root) {
  return root->CreateUintMetric(kName, 0);
}

template <>
NumericMetric<double> CreateMetric<double>(Object* root) {
  return root->CreateDoubleMetric(kName, 0);
}

template <typename T>
bool TestMetricLifecycle(perftest::RepeatState* state) {
  auto inspector = Inspector();
  auto root = inspector.CreateObject("objects");
  state->DeclareStep("Create");
  state->DeclareStep("Destroy");
  while (state->KeepRunning()) {
    auto item = CreateMetric<T>(&root);
    state->NextStep();
  }
  return true;
}

bool TestIntMetricLifecycle(perftest::RepeatState* state) {
  return TestMetricLifecycle<int64_t>(state);
}

bool TestUintMetricLifecycle(perftest::RepeatState* state) {
  return TestMetricLifecycle<uint64_t>(state);
}

bool TestDoubleMetricLifecycle(perftest::RepeatState* state) {
  return TestMetricLifecycle<double>(state);
}

// Measure the time taken to set and modify NumericMetric.
template <typename T>
bool TestMetricModify(perftest::RepeatState* state) {
  auto inspector = Inspector();
  auto root = inspector.CreateObject("objects");
  auto item = CreateMetric<T>(&root);

  state->DeclareStep("Set");
  state->DeclareStep("Add");
  state->DeclareStep("Subtract");

  while (state->KeepRunning()) {
    item.Set(1);
    state->NextStep();
    item.Add(1);
    state->NextStep();
    item.Subtract(1);
  }
  return true;
}

bool TestIntMetricModify(perftest::RepeatState* state) {
  return TestMetricModify<int64_t>(state);
}

bool TestUintMetricModify(perftest::RepeatState* state) {
  return TestMetricModify<uint64_t>(state);
}

bool TestDoubleMetricModify(perftest::RepeatState* state) {
  return TestMetricModify<double>(state);
}

// Measure the time taken to set and modify Property.
bool TestProperty(perftest::RepeatState* state, int size) {
  auto inspector =
      Inspector(1024 * 1024 /*capacity*/, 1024 * 1024 /*max size*/);
  auto root = inspector.CreateObject("objects");
  auto item =
      root.CreateProperty(kName, "", inspect::vmo::PropertyFormat::kUtf8);
  std::string string;
  string.resize(size, 'a');

  state->DeclareStep("Create");
  state->DeclareStep("Set");
  state->DeclareStep("SetAgain");
  state->DeclareStep("Destroy");

  while (state->KeepRunning()) {
    auto item =
        root.CreateProperty(kName, "", inspect::vmo::PropertyFormat::kUtf8);
    state->NextStep();
    item.Set(string);
    state->NextStep();
    item.Set(string);
    state->NextStep();
  }
  return true;
}

void RegisterTests() {
  perftest::RegisterTest("Inspect/IntMetric/Lifecycle", TestIntMetricLifecycle);
  perftest::RegisterTest("Inspect/IntMetric/Modify", TestIntMetricModify);
  perftest::RegisterTest("Inspect/UintMetric/Lifecycle",
                         TestUintMetricLifecycle);
  perftest::RegisterTest("Inspect/UintMetric/Modify", TestUintMetricModify);
  perftest::RegisterTest("Inspect/DoubleMetric/Lifecycle",
                         TestDoubleMetricLifecycle);
  perftest::RegisterTest("Inspect/DoubleMetric/Modify", TestDoubleMetricModify);
  for (auto size : {4, 8, 100, 2000, 2048, 10000}) {
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/Property/%d", size).c_str(), TestProperty,
        size);
  }
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
