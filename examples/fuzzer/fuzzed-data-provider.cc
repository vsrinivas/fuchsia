// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A fuzzer that uses the FuzzedDataProvider library to consume fuzzing input.
// more info:
// https://github.com/google/fuzzing/blob/master/docs/split-inputs.md#fuzzed-data-provider
// https://github.com/llvm/llvm-project/blob/master/compiler-rt/include/fuzzer/FuzzedDataProvider.h
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <fuzzer/FuzzedDataProvider.h>

// In order to consume Enums in FuzzedDataProvider, your enums need
// an element named kMaxValue, pointing to the maximum value of the enum.
enum Color { kRed, kBlue, kYellow, kMaxValue = kYellow };

using bar = struct {
  uint32_t anint;
  double adouble;
  const char *str;
  Color color;
};

static const uint16_t kVal1Threshold = 15000;
static const uint16_t kMinVal1 = 13000;
static const uint16_t kMaxVal1 = 16000;
static const uint8_t kVal2Magic = 105;
static const uint32_t kUint32Magic = 313131;
static const char *kMagicString = "magicstring";
static const size_t kMaxStrLen = 20;
static const double kDoubleThreshold = 100;

int foo_function(uint16_t val1, uint8_t val2, bool val3, bar *val4) {
  // This code is irrelevant, just uses the values received as parameters.
  if (val4 == nullptr || val4->str == nullptr) {
    return 0;
  }
  if (val1 > kVal1Threshold && val2 == kVal2Magic && !val3 && val4->anint == kUint32Magic) {
    __builtin_trap();
  }
  if (strncmp(val4->str, kMagicString, strlen(kMagicString) + 1) == 0 &&
      val4->adouble < kDoubleThreshold) {
    __builtin_trap();
  }

  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider fuzzed_data(data, size);

  auto val1 = fuzzed_data.ConsumeIntegralInRange<uint16_t>(kMinVal1, kMaxVal1);
  auto val2 = fuzzed_data.ConsumeIntegral<uint8_t>();
  auto val3 = fuzzed_data.ConsumeBool();
  auto str = fuzzed_data.ConsumeRandomLengthString(kMaxStrLen);

  bar val4 = {.anint = fuzzed_data.ConsumeIntegral<uint32_t>(),
              .adouble = fuzzed_data.ConsumeFloatingPoint<double>(),
              .str = str.c_str(),
              .color = fuzzed_data.ConsumeEnum<Color>()};

  return foo_function(val1, val2, val3, &val4);
}
