// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/vmo/heap.h>
#include <zircon/syscalls.h>

#include <cmath>
#include <iostream>
#include <sstream>

#include <fbl/ref_ptr.h>
#include <fbl/string_printf.h>
#include <perftest/perftest.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace {

const char* kName = "name";
const size_t kLinearFloor = 10;
const size_t kLinearStep = 5;
const size_t kExponentialFloor = 10;
const size_t kExponentialInitialStep = 5;
const size_t kExponentialStepMultiplier = 2;

using inspect::Inspector;
using inspect::Node;
using inspect::internal::BlockIndex;
using inspect::internal::Heap;
using inspect::internal::NumericProperty;

template <typename T>
NumericProperty<T> CreateMetric(Node* root);

template <>
NumericProperty<int64_t> CreateMetric<int64_t>(Node* root) {
  return root->CreateInt(kName, 0);
}

template <>
NumericProperty<uint64_t> CreateMetric<uint64_t>(Node* root) {
  return root->CreateUint(kName, 0);
}

template <>
NumericProperty<double> CreateMetric<double>(Node* root) {
  return root->CreateDouble(kName, 0);
}

template <typename T>
T CreateArrayMetric(Node* root, size_t size);

template <>
inspect::IntArray CreateArrayMetric(Node* root, size_t size) {
  return root->CreateIntArray(kName, size);
}

template <>
inspect::UintArray CreateArrayMetric(Node* root, size_t size) {
  return root->CreateUintArray(kName, size);
}

template <>
inspect::DoubleArray CreateArrayMetric(Node* root, size_t size) {
  return root->CreateDoubleArray(kName, size);
}

template <>
inspect::LinearIntHistogram CreateArrayMetric(Node* root, size_t size) {
  return root->CreateLinearIntHistogram(kName, kLinearFloor, kLinearStep, size);
}

template <>
inspect::LinearUintHistogram CreateArrayMetric(Node* root, size_t size) {
  return root->CreateLinearUintHistogram(kName, kLinearFloor, kLinearStep, size);
}

template <>
inspect::LinearDoubleHistogram CreateArrayMetric(Node* root, size_t size) {
  return root->CreateLinearDoubleHistogram(kName, kLinearFloor, kLinearStep, size);
}

template <>
inspect::ExponentialIntHistogram CreateArrayMetric(Node* root, size_t size) {
  return root->CreateExponentialIntHistogram(kName, kExponentialFloor, kExponentialInitialStep,
                                             kExponentialStepMultiplier, size);
}

template <>
inspect::ExponentialUintHistogram CreateArrayMetric(Node* root, size_t size) {
  return root->CreateExponentialUintHistogram(kName, kExponentialFloor, kExponentialInitialStep,
                                              kExponentialStepMultiplier, size);
}

template <>
inspect::ExponentialDoubleHistogram CreateArrayMetric(Node* root, size_t size) {
  return root->CreateExponentialDoubleHistogram(kName, kExponentialFloor, kExponentialInitialStep,
                                                kExponentialStepMultiplier, size);
}

bool TestNodeLifecycle(perftest::RepeatState* state) {
  auto inspector = Inspector();
  auto& root = inspector.GetRoot();
  state->DeclareStep("Create");
  state->DeclareStep("Destroy");
  while (state->KeepRunning()) {
    auto node = root.CreateChild(kName);
    state->NextStep();
  }
  return true;
}

bool TestValueListLifecycle(perftest::RepeatState* state) {
  struct Dummy {
    uint64_t value;
  };
  state->DeclareStep("Create");
  state->DeclareStep("Enlist");
  state->DeclareStep("EnlistAgain");
  state->DeclareStep("Destroy");
  while (state->KeepRunning()) {
    inspect::ValueList list;
    state->NextStep();
    list.emplace(Dummy{.value = 0});
    state->NextStep();
    list.emplace(Dummy{.value = 1});
    state->NextStep();
  }
  return true;
}

template <typename T>
bool TestMetricLifecycle(perftest::RepeatState* state) {
  auto inspector = Inspector();
  auto& root = inspector.GetRoot();
  state->DeclareStep("Create");
  state->DeclareStep("Destroy");
  while (state->KeepRunning()) {
    auto item = CreateMetric<T>(&root);
    state->NextStep();
  }
  return true;
}

template <typename T>
bool TestArrayLifecycle(perftest::RepeatState* state, size_t size) {
  auto inspector = Inspector();
  auto& root = inspector.GetRoot();
  state->DeclareStep("Create");
  state->DeclareStep("Destroy");
  while (state->KeepRunning()) {
    auto item = CreateArrayMetric<T>(&root, size);
    state->NextStep();
  }
  return true;
}

// Measure the time taken to set and modify NumericProperty.
template <typename T>
bool TestMetricModify(perftest::RepeatState* state) {
  auto inspector = Inspector();
  auto& root = inspector.GetRoot();
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

template <typename T>
bool TestArrayModify(perftest::RepeatState* state, int size) {
  auto inspector = Inspector(inspect::InspectSettings{.maximum_size = 1024 * 1024});
  auto& root = inspector.GetRoot();
  auto item = CreateArrayMetric<T>(&root, size);

  state->DeclareStep("Set");
  state->DeclareStep("Add");
  state->DeclareStep("Subtract");

  int i = 0;
  while (state->KeepRunning()) {
    item.Set(i, 1);
    state->NextStep();
    item.Add(i, 1);
    state->NextStep();
    item.Subtract(i, 1);
    i = (i + 1) % size;
  }
  return true;
}

template <typename T>
bool TestHistogramInsert(perftest::RepeatState* state, int size, int value) {
  auto inspector = Inspector(inspect::InspectSettings{.maximum_size = 1024 * 1024});
  auto& root = inspector.GetRoot();
  auto item = CreateArrayMetric<T>(&root, size);

  const int underflow_value = 0;
  const int overflow_value = 10000000;

  state->DeclareStep("InsertUnderflow");
  state->DeclareStep("InsertOverflow");
  state->DeclareStep("InsertValue");

  while (state->KeepRunning()) {
    item.Insert(underflow_value);
    state->NextStep();
    item.Insert(overflow_value);
    state->NextStep();
    item.Insert(value);
  }
  return true;
}

// Measure the time taken to set and modify Property.
bool TestProperty(perftest::RepeatState* state, int size) {
  auto inspector = Inspector(inspect::InspectSettings{.maximum_size = 1024 * 1024});
  auto& root = inspector.GetRoot();
  auto item = root.CreateString(kName, "");
  std::string string;
  string.resize(size, 'a');

  state->DeclareStep("Create");
  state->DeclareStep("Set");
  state->DeclareStep("SetAgain");
  state->DeclareStep("Destroy");

  while (state->KeepRunning()) {
    auto item = root.CreateString(kName, "");
    state->NextStep();
    item.Set(string);
    state->NextStep();
    item.Set(string);
    state->NextStep();
  }
  return true;
}

// Measure how long it takes to allocate and extend a heap.
bool TestHeapExtend(perftest::RepeatState* state) {
  zx::vmo vmo;

  state->DeclareStep("Create 1MB VMO");
  state->DeclareStep("Allocate 512K");
  state->DeclareStep("Extend");
  state->DeclareStep("Free");
  state->DeclareStep("Destroy");

  while (state->KeepRunning()) {
    BlockIndex index[513];
    if (zx::vmo::create(1 << 21, 0, &vmo) != ZX_OK) {
      return false;
    }

    auto heap = Heap(std::move(vmo));
    state->NextStep();

    for (int i = 0; i < 512; i++) {
      if (heap.Allocate(2048, &index[i]) != ZX_OK) {
        return false;
      }
    }
    state->NextStep();

    if (heap.Allocate(2048, &index[512]) != ZX_OK) {
      return false;
    }

    state->NextStep();

    for (int i = 512; i >= 0; i--) {
      heap.Free(index[i]);
    }
    state->NextStep();
  }

  return true;
}

void RegisterTests() {
  perftest::RegisterTest("Inspect/ValueList/Lifecycle", TestValueListLifecycle);
  perftest::RegisterTest("Inspect/Node/Lifecycle", TestNodeLifecycle);
  perftest::RegisterTest("Inspect/IntMetric/Lifecycle", TestMetricLifecycle<int64_t>);
  perftest::RegisterTest("Inspect/IntMetric/Modify", TestMetricModify<int64_t>);
  perftest::RegisterTest("Inspect/UintMetric/Lifecycle", TestMetricLifecycle<uint64_t>);
  perftest::RegisterTest("Inspect/UintMetric/Modify", TestMetricModify<uint64_t>);
  perftest::RegisterTest("Inspect/DoubleMetric/Lifecycle", TestMetricLifecycle<double>);
  perftest::RegisterTest("Inspect/DoubleMetric/Modify", TestMetricModify<double>);
  for (auto size : {32, 128, 240}) { /* stop at 240 to fit in block */
    perftest::RegisterTest(fxl::StringPrintf("Inspect/UintArray/Lifecycle/%d", size).c_str(),
                           TestArrayLifecycle<inspect::UintArray>, size);
    perftest::RegisterTest(fxl::StringPrintf("Inspect/UintArray/Modify/%d", size).c_str(),
                           TestArrayModify<inspect::UintArray>, size);
    perftest::RegisterTest(fxl::StringPrintf("Inspect/IntArray/Lifecycle/%d", size).c_str(),
                           TestArrayLifecycle<inspect::IntArray>, size);
    perftest::RegisterTest(fxl::StringPrintf("Inspect/IntArray/Modify/%d", size).c_str(),
                           TestArrayModify<inspect::IntArray>, size);
    perftest::RegisterTest(fxl::StringPrintf("Inspect/DoubleArray/Lifecycle/%d", size).c_str(),
                           TestArrayLifecycle<inspect::DoubleArray>, size);
    perftest::RegisterTest(fxl::StringPrintf("Inspect/DoubleArray/Modify/%d", size).c_str(),
                           TestArrayModify<inspect::DoubleArray>, size);

    const auto linear_midpoint = kLinearFloor + (size / 2) * kLinearStep;
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/LinearUintHistogram/Lifecycle/%d", size).c_str(),
        TestArrayLifecycle<inspect::LinearUintHistogram>, size);
    perftest::RegisterTest(fxl::StringPrintf("Inspect/LinearUintHistogram/Insert/%d", size).c_str(),
                           TestHistogramInsert<inspect::LinearUintHistogram>, size,
                           linear_midpoint);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/LinearIntHistogram/Lifecycle/%d", size).c_str(),
        TestArrayLifecycle<inspect::LinearIntHistogram>, size);
    perftest::RegisterTest(fxl::StringPrintf("Inspect/LinearIntHistogram/Insert/%d", size).c_str(),
                           TestHistogramInsert<inspect::LinearIntHistogram>, size, linear_midpoint);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/LinearDoubleHistogram/Lifecycle/%d", size).c_str(),
        TestArrayLifecycle<inspect::LinearDoubleHistogram>, size);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/LinearDoubleHistogram/Insert/%d", size).c_str(),
        TestHistogramInsert<inspect::LinearDoubleHistogram>, size, linear_midpoint);
  }
  for (auto size : {4, 16, 32}) {
    const auto exponential_midpoint =
        (kExponentialFloor +
         kExponentialInitialStep * std::pow(kExponentialStepMultiplier, size / 2));
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/ExponentialUintHistogram/Lifecycle/%d", size).c_str(),
        TestArrayLifecycle<inspect::ExponentialUintHistogram>, size);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/ExponentialUintHistogram/Insert/%d", size).c_str(),
        TestHistogramInsert<inspect::ExponentialUintHistogram>, size, exponential_midpoint);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/ExponentialIntHistogram/Lifecycle/%d", size).c_str(),
        TestArrayLifecycle<inspect::ExponentialIntHistogram>, size);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/ExponentialIntHistogram/Insert/%d", size).c_str(),
        TestHistogramInsert<inspect::ExponentialIntHistogram>, size, exponential_midpoint);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/ExponentialDoubleHistogram/Lifecycle/%d", size).c_str(),
        TestArrayLifecycle<inspect::ExponentialDoubleHistogram>, size);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/ExponentialDoubleHistogram/Insert/%d", size).c_str(),
        TestHistogramInsert<inspect::ExponentialDoubleHistogram>, size, exponential_midpoint);
  }
  for (auto size : {4, 8, 100, 2000, 2048, 10000}) {
    perftest::RegisterTest(fxl::StringPrintf("Inspect/Property/%d", size).c_str(), TestProperty,
                           size);
  }
  perftest::RegisterTest("Inspect/Heap/Extend", TestHeapExtend);
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
