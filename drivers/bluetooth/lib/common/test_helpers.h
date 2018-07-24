// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_TEST_HELPERS_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_TEST_HELPERS_H_

#include <algorithm>
#include <iostream>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"

#include "lib/fxl/strings/string_printf.h"

namespace btlib {
namespace common {

// Function-template for comparing contents of two iterable byte containers for
// equality. If the contents are not equal, this logs a GTEST-style error
// message to stdout. Meant to be used from unit tests.
template <class InputIt1, class InputIt2>
bool ContainersEqual(InputIt1 expected_begin, InputIt1 expected_end,
                     InputIt2 actual_begin, InputIt2 actual_end) {
  if (std::equal(expected_begin, expected_end, actual_begin, actual_end))
    return true;
  std::cout << "Expected: { ";
  for (InputIt1 iter = expected_begin; iter != expected_end; ++iter) {
    std::cout << fxl::StringPrintf("0x%02x ", *iter);
  }
  std::cout << "}\n   Found: { ";
  for (InputIt2 iter = actual_begin; iter != actual_end; ++iter) {
    std::cout << fxl::StringPrintf("0x%02x ", *iter);
  }
  std::cout << "}" << std::endl;
  return false;
}

template <class Container1, class Container2>
bool ContainersEqual(const Container1& expected, const Container2& actual) {
  return ContainersEqual(expected.begin(), expected.end(), actual.begin(),
                         actual.end());
}

template <class Container1>
bool ContainersEqual(const Container1& expected, const uint8_t* actual_bytes,
                     size_t actual_num_bytes) {
  return ContainersEqual(expected.begin(), expected.end(), actual_bytes,
                         actual_bytes + actual_num_bytes);
}

// Returns a managed pointer to a heap allocated MutableByteBuffer.
template <typename... T>
common::MutableByteBufferPtr NewBuffer(T... bytes) {
  return std::make_unique<common::StaticByteBuffer<sizeof...(T)>>(
      std::forward<T>(bytes)...);
}

// Returns the Upper/Lower bits of a uint16_t
constexpr uint8_t UpperBits(const uint16_t x) { return x >> 8; }
constexpr uint8_t LowerBits(const uint16_t x) { return x & 0x00FF; }

}  // namespace common
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_TEST_HELPERS_H_
