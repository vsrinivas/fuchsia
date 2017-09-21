// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <algorithm>
#include <iostream>

#include "lib/fxl/strings/string_printf.h"

namespace bluetooth {
namespace common {

// Function-template for comparing contents of two iterable byte containers for
// equality. If the contents are not equal, this logs a GTEST-style error
// message to stdout. Meant to be used from unit tests.
template <class InputIt1, class InputIt2>
bool ContainersEqual(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2) {
  if (std::equal(first1, last1, first2, last2)) return true;
  std::cout << "Expected: { ";
  for (InputIt1 iter = first1; iter != last1; ++iter) {
    std::cout << fxl::StringPrintf("0x%02x ", *iter);
  }
  std::cout << "}\n   Found: { ";
  for (InputIt2 iter = first2; iter != last2; ++iter) {
    std::cout << fxl::StringPrintf("0x%02x ", *iter);
  }
  std::cout << "}" << std::endl;
  return false;
}

template <class Container1, class Container2>
bool ContainersEqual(const Container1& c1, const Container2& c2) {
  return ContainersEqual(c1.begin(), c1.end(), c2.begin(), c2.end());
}

template <class Container1>
bool ContainersEqual(const Container1& c1, const uint8_t* bytes, size_t num_bytes) {
  return ContainersEqual(c1.begin(), c1.end(), bytes, bytes + num_bytes);
}

}  // namespace common
}  // namespace bluetooth
