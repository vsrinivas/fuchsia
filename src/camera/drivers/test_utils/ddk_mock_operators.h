// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_TEST_UTILS_DDK_MOCK_OPERATORS_H_
#define SRC_CAMERA_DRIVERS_TEST_UTILS_DDK_MOCK_OPERATORS_H_

#include <fuchsia/hardware/camera/sensor/c/banjo.h>

// The following equality operators are necessary for ddk mocks.

static bool operator==(const color_val& lhs, const color_val& rhs) {
  return lhs.blue == rhs.blue && lhs.green_b == rhs.green_b && lhs.green_r == rhs.green_r &&
         lhs.red == rhs.red;
}

static bool operator==(const rect& lhs, const rect& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height && lhs.x == rhs.x && lhs.y == rhs.y;
}

static bool operator==(const dimensions_t& lhs, const dimensions_t& rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y;
}

static bool operator==(const min_max_constraints_t& lhs, const min_max_constraints_t& rhs) {
  return lhs.min == rhs.min && lhs.max == rhs.max;
}

static bool operator==(const extension_value_data_type& lhs, const extension_value_data_type& rhs) {
  return lhs.byte_array_value == rhs.byte_array_value &&
         lhs.constraints_value == rhs.constraints_value &&
         lhs.frame_rate_info_value == rhs.frame_rate_info_value && lhs.int_value == rhs.int_value &&
         lhs.dimension_value == rhs.dimension_value && lhs.uint_value == rhs.uint_value;
}

#endif  // SRC_CAMERA_DRIVERS_TEST_UTILS_DDK_MOCK_OPERATORS_H_
