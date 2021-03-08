// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_TEST_UTILS_H_
#define SRC_LIB_FUZZING_FIDL_TEST_UTILS_H_

#include <stddef.h>
#include <stdint.h>

#include <limits>
#include <random>
#include <vector>

#include <gtest/gtest.h>

namespace fuzzing {

// Helper function to create a deterministically pseudorandom integer type.
template <typename T>
T Pick() {
  static std::mt19937_64 prng;
  return static_cast<T>(prng() & std::numeric_limits<T>::max());
}

// Helper function to create an array of deterministically pseudorandom integer types.
template <typename T>
void PickArray(T* out, size_t out_len) {
  for (size_t i = 0; i < out_len; ++i) {
    out[i] = Pick<T>();
  }
}

// Helper function to create a vector of deterministically pseudorandom integer types.
template <typename T = uint8_t>
std::vector<T> PickVector(size_t size) {
  std::vector<T> v;
  v.reserve(size);
  for (size_t i = 0; i < size; ++i) {
    v.push_back(Pick<T>());
  }
  return v;
}

}  // namespace fuzzing

#endif  // SRC_LIB_FUZZING_FIDL_TEST_UTILS_H_
