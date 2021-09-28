// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_COVERAGE_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_COVERAGE_H_

#include <stddef.h>
#include <stdint.h>

#include <vector>

namespace fuzzing {

using Coverage = std::vector<std::pair<size_t, uint8_t>>;

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_COVERAGE_H_
