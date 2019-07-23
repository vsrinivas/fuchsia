// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_EPSILON_COMPARE_H_
#define SRC_UI_LIB_ESCHER_UTIL_EPSILON_COMPARE_H_

#include <glm/glm.hpp>

namespace escher {

// Returns true iff |f0| and |f1| are the same within optional |epsilon|.
bool CompareFloat(float f0, float f1, float epsilon = 0.0);
bool CompareMatrix(glm::mat4 m0, glm::mat4 m1, float epsilon = 0.0);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_EPSILON_COMPARE_H_
