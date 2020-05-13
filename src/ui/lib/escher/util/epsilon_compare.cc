// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/util/epsilon_compare.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/lib/escher/geometry/types.h"

namespace escher {

static constexpr bool EPSILON_ERROR_DETAIL = false;

bool CompareFloat(float f0, float f1, float epsilon) {
  bool compare = glm::abs(f0 - f1) <= epsilon;
  if (!compare && EPSILON_ERROR_DETAIL)
    FX_LOGS(WARNING) << "floats " << f0 << " and " << f1 << " differ by " << glm::abs(f0 - f1)
                     << " which is greater than provided epsilon " << epsilon;
  return compare;
}

bool CompareMatrix(glm::mat4 m0, glm::mat4 m1, float epsilon) {
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
    FX_LOGS(WARNING) << "The following matrices differ:\n";
    FX_LOGS(WARNING) << m0;
    FX_LOGS(WARNING) << m1;
  }
  return compare;
}

}  // namespace escher
