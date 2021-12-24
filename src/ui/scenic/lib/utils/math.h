// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_UTILS_MATH_H_
#define SRC_UI_SCENIC_LIB_UTILS_MATH_H_

#include <array>

#include <glm/glm.hpp>

namespace utils {

// Homogenizes |vector|. Does not perform safety checks beyond if vector.w == 0.
glm::vec4 Homogenize(const glm::vec4& vector);

// Applies |transform| to |pointer| by converting it to 3D and back again.
glm::vec2 TransformPointerCoords(const glm::vec2& pointer, const glm::mat4& transform);

// Transforms a column major mat3 in array format to a glm::mat4.
glm::mat4 ColumnMajorMat3VectorToMat4(const std::array<float, 9>& matrix_array);

}  // namespace utils

#endif  // SRC_UI_SCENIC_LIB_UTILS_MATH_H_
