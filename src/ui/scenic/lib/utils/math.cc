// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/utils/math.h"

namespace utils {

glm::vec4 Homogenize(const glm::vec4& vector) {
  if (vector.w == 0.f) {
    return vector;
  }
  return vector / vector.w;
}

glm::vec2 TransformPointerCoords(const glm::vec2& pointer, const glm::mat4& transform) {
  const glm::vec4 homogenous_pointer{pointer.x, pointer.y, 0, 1};
  const glm::vec4 transformed_pointer = transform * homogenous_pointer;
  const glm::vec2 homogenized_transformed_pointer{Homogenize(transformed_pointer)};
  return homogenized_transformed_pointer;
}

std::array<float, 9> Mat4ToColumnMajorMat3Array(const glm::mat4& mat) {
  return {mat[0][0], mat[0][1], mat[0][3], mat[1][0], mat[1][1],
          mat[1][3], mat[3][0], mat[3][1], mat[3][3]};
}

glm::mat4 ColumnMajorMat3ArrayToMat4(const std::array<float, 9>& matrix_array) {
  // clang-format off
  return glm::mat4(matrix_array[0], matrix_array[1], 0.f, matrix_array[2],  // first column
                   matrix_array[3], matrix_array[4], 0.f, matrix_array[5],  // second column
                               0.f,             0.f, 1.f,             0.f,  // third column
                   matrix_array[6], matrix_array[7], 0.f,             1.f); // fourth column
  // clang-format on
}

}  // namespace utils
