// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_MICROBENCHMARKS_UTIL_H_
#define SRC_TESTS_MICROBENCHMARKS_UTIL_H_

#include <vector>

namespace util {

std::vector<std::string> MakeDeterministicNamesList(size_t length);

}  // namespace util

#endif  // SRC_TESTS_MICROBENCHMARKS_UTIL_H_
