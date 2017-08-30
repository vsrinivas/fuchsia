// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains utility functions for reading and parsing validation data
// files.

#ifndef LIB_FIDL_CPP_BINDINGS_TESTS_VALIDATION_UTIL_H_
#define LIB_FIDL_CPP_BINDINGS_TESTS_VALIDATION_UTIL_H_

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace fidl {
namespace test {
namespace validation_util {

// Given |test_name|, which is the filename of a test file (excluding the
// extension), reads in and parses the binary test data (stored in |data|), the
// number of expected handles (stored in |num_handles|), and the expected
// validation string (stored in |expected|).
bool ReadTestCase(const std::string& test_name,
                  std::vector<uint8_t>* data,
                  size_t* num_handles,
                  std::string* expected);

// Gets a list of matching tests with prefix |prefix|; these list of tests are
// file names of the tests, without the '.data' extension.
std::vector<std::string> GetMatchingTests(const std::string& prefix);

}  // namespace validation_util
}  // namespace test
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_TESTS_VALIDATION_UTIL_H_
