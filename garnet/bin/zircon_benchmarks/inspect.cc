// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/inspect-vmo/inspect.h"

#include <fbl/ref_ptr.h>
#include <fbl/string_printf.h>
#include <perftest/perftest.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <zircon/syscalls.h>

#include <cmath>
#include <iostream>
#include <sstream>

#include "lib/inspect-vmo/types.h"

namespace {

const char* kName = "name";
const size_t kLinearFloor = 10;
const size_t kLinearStep = 5;
const size_t kExponentialFloor = 10;
const size_t kExponentialInitialStep = 5;
const size_t kExponentialStepMultiplier = 2;

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
T CreateArrayMetric(Object* root, size_t size);

template <>
inspect::vmo::IntArray CreateArrayMetric(Object* root, size_t size) {
  return root->CreateIntArray(kName, size, inspect::vmo::ArrayFormat::kDefault);
}

template <>
inspect::vmo::UintArray CreateArrayMetric(Object* root, size_t size) {
  return root->CreateUintArray(kName, size,
                               inspect::vmo::ArrayFormat::kDefault);
}

template <>
inspect::vmo::DoubleArray CreateArrayMetric(Object* root, size_t size) {
  return root->CreateDoubleArray(kName, size,
                                 inspect::vmo::ArrayFormat::kDefault);
}

template <>
inspect::vmo::LinearIntHistogram CreateArrayMetric(Object* root, size_t size) {
  return root->CreateLinearIntHistogram(kName, kLinearFloor, kLinearStep, size);
}

template <>
inspect::vmo::LinearUintHistogram CreateArrayMetric(Object* root, size_t size) {
  return root->CreateLinearUintHistogram(kName, kLinearFloor, kLinearStep,
                                         size);
}

template <>
inspect::vmo::LinearDoubleHistogram CreateArrayMetric(Object* root,
                                                      size_t size) {
  return root->CreateLinearDoubleHistogram(kName, kLinearFloor, kLinearStep,
                                           size);
}

template <>
inspect::vmo::ExponentialIntHistogram CreateArrayMetric(Object* root,
                                                        size_t size) {
  return root->CreateExponentialIntHistogram(kName, kExponentialFloor,
                                             kExponentialInitialStep,
                                             kExponentialStepMultiplier, size);
}

template <>
inspect::vmo::ExponentialUintHistogram CreateArrayMetric(Object* root,
                                                         size_t size) {
  return root->CreateExponentialUintHistogram(kName, kExponentialFloor,
                                              kExponentialInitialStep,
                                              kExponentialStepMultiplier, size);
}

template <>
inspect::vmo::ExponentialDoubleHistogram CreateArrayMetric(Object* root,
                                                           size_t size) {
  return root->CreateExponentialDoubleHistogram(
      kName, kExponentialFloor, kExponentialInitialStep,
      kExponentialStepMultiplier, size);
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

template <typename T>
bool TestArrayLifecycle(perftest::RepeatState* state, size_t size) {
  auto inspector = Inspector();
  auto root = inspector.CreateObject("objects");
  state->DeclareStep("Create");
  state->DeclareStep("Destroy");
  while (state->KeepRunning()) {
    auto item = CreateArrayMetric<T>(&root, size);
    state->NextStep();
  }
  return true;
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

template <typename T>
bool TestArrayModify(perftest::RepeatState* state, int size) {
  auto inspector =
      Inspector(1024 * 1024 /*capacity*/, 1024 * 1024 /*max size*/);
  auto root = inspector.CreateObject("objects");
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
  auto inspector =
      Inspector(1024 * 1024 /*capacity*/, 1024 * 1024 /*max size*/);
  auto root = inspector.CreateObject("objects");
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
  perftest::RegisterTest("Inspect/IntMetric/Lifecycle",
                         TestMetricLifecycle<int64_t>);
  perftest::RegisterTest("Inspect/IntMetric/Modify", TestMetricModify<int64_t>);
  perftest::RegisterTest("Inspect/UintMetric/Lifecycle",
                         TestMetricLifecycle<uint64_t>);
  perftest::RegisterTest("Inspect/UintMetric/Modify",
                         TestMetricModify<uint64_t>);
  perftest::RegisterTest("Inspect/DoubleMetric/Lifecycle",
                         TestMetricLifecycle<double>);
  perftest::RegisterTest("Inspect/DoubleMetric/Modify",
                         TestMetricModify<double>);
  for (auto size : {32, 128, 240}) { /* stop at 240 to fit in block */
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/UintArray/Lifecycle/%d", size).c_str(),
        TestArrayLifecycle<inspect::vmo::UintArray>, size);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/UintArray/Modify/%d", size).c_str(),
        TestArrayModify<inspect::vmo::UintArray>, size);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/IntArray/Lifecycle/%d", size).c_str(),
        TestArrayLifecycle<inspect::vmo::IntArray>, size);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/IntArray/Modify/%d", size).c_str(),
        TestArrayModify<inspect::vmo::IntArray>, size);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/DoubleArray/Lifecycle/%d", size).c_str(),
        TestArrayLifecycle<inspect::vmo::DoubleArray>, size);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/DoubleArray/Modify/%d", size).c_str(),
        TestArrayModify<inspect::vmo::DoubleArray>, size);

    const auto linear_midpoint = kLinearFloor + (size / 2) * kLinearStep;
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/LinearUintHistogram/Lifecycle/%d", size)
            .c_str(),
        TestArrayLifecycle<inspect::vmo::LinearUintHistogram>, size);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/LinearUintHistogram/Insert/%d", size)
            .c_str(),
        TestHistogramInsert<inspect::vmo::LinearUintHistogram>, size,
        linear_midpoint);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/LinearIntHistogram/Lifecycle/%d", size)
            .c_str(),
        TestArrayLifecycle<inspect::vmo::LinearIntHistogram>, size);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/LinearIntHistogram/Insert/%d", size).c_str(),
        TestHistogramInsert<inspect::vmo::LinearIntHistogram>, size,
        linear_midpoint);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/LinearDoubleHistogram/Lifecycle/%d", size)
            .c_str(),
        TestArrayLifecycle<inspect::vmo::LinearDoubleHistogram>, size);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/LinearDoubleHistogram/Insert/%d", size)
            .c_str(),
        TestHistogramInsert<inspect::vmo::LinearDoubleHistogram>, size,
        linear_midpoint);

    const auto exponential_midpoint =
        (kExponentialFloor +
         kExponentialInitialStep *
             std::pow(kExponentialStepMultiplier, size / 2));
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/ExponentialUintHistogram/Lifecycle/%d", size)
            .c_str(),
        TestArrayLifecycle<inspect::vmo::ExponentialUintHistogram>, size);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/ExponentialUintHistogram/Insert/%d", size)
            .c_str(),
        TestHistogramInsert<inspect::vmo::ExponentialUintHistogram>, size,
        exponential_midpoint);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/ExponentialIntHistogram/Lifecycle/%d", size)
            .c_str(),
        TestArrayLifecycle<inspect::vmo::ExponentialIntHistogram>, size);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/ExponentialIntHistogram/Insert/%d", size)
            .c_str(),
        TestHistogramInsert<inspect::vmo::ExponentialIntHistogram>, size,
        exponential_midpoint);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/ExponentialDoubleHistogram/Lifecycle/%d",
                          size)
            .c_str(),
        TestArrayLifecycle<inspect::vmo::ExponentialDoubleHistogram>, size);
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/ExponentialDoubleHistogram/Insert/%d", size)
            .c_str(),
        TestHistogramInsert<inspect::vmo::ExponentialDoubleHistogram>, size,
        exponential_midpoint);
  }
  for (auto size : {4, 8, 100, 2000, 2048, 10000}) {
    perftest::RegisterTest(
        fxl::StringPrintf("Inspect/Property/%d", size).c_str(), TestProperty,
        size);
  }
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
