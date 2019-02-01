// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_EPSILON_COMPARE_H_
#define LIB_ESCHER_UTIL_EPSILON_COMPARE_H_

#include <iomanip>
#include <ostream>

namespace escher {

static constexpr bool EPSILON_ERROR_DETAIL = false;

// Returns true iff |f0| and |f1| are the same within optional |epsilon|.
bool CompareFloat(float f0, float f1, float epsilon = 0.0) {
  bool compare = glm::abs(f0 - f1) <= epsilon;
  if (!compare && EPSILON_ERROR_DETAIL)
    FXL_LOG(WARNING) << "floats " << f0 << " and " << f1 << " differ by "
                     << glm::abs(f0 - f1)
                     << " which is greater than provided epsilon " << epsilon;
  return compare;
}

bool CompareMatrix(glm::mat4 m0, glm::mat4 m1, float epsilon = 0.0) {
  bool compare = true;
  for (uint32_t i = 0; i < 4; i++) {
    for (uint32_t j = 0; j < 4; j++) {
      compare = compare && CompareFloat(m0[i][j], m1[i][j], epsilon);
      if (!compare) {
        break;
      }
    }
  }

  if (!compare && EPSILON_ERROR_DETAIL) {
    FXL_LOG(WARNING) << "The following matrices differ:\n";
    FXL_LOG(WARNING) << m0;
    FXL_LOG(WARNING) << m1;
  }
  return compare;
}

}  // namespace escher
#endif  // LIB_ESCHER_UTIL_EPSILON_COMPARE_H_
