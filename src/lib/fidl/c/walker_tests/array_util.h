// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_C_WALKER_TESTS_ARRAY_UTIL_H_
#define SRC_LIB_FIDL_C_WALKER_TESTS_ARRAY_UTIL_H_

// All sizes in fidl encoding tables are 16 bits. The fidl compiler
// normally enforces this. Check manually in manual tests.
template <typename T, size_t N>
uint16_t ArrayCount(T const (&array)[N]) {
  static_assert(N < UINT32_MAX, "Array is too large!");
  return N;
}

template <typename T, size_t N>
uint32_t ArraySize(T const (&array)[N]) {
  static_assert(sizeof(array) < UINT16_MAX, "Array is too large!");
  return sizeof(array);
}

#endif  // SRC_LIB_FIDL_C_WALKER_TESTS_ARRAY_UTIL_H_
