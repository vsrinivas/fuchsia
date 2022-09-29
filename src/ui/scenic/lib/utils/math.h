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

// Converts a glm::mat4 to an array of a mat3 in column major order by shaving off the third row
// and column. This is valid for 2D-in-3D transforms affecting the xy-plane (i.e. how 2D content is
// handled in GFX).
//      Mat4                Mat3                   array
// [  1  2  3  4 ]      [  1  2  4 ]
// [  5  6  7  8 ]  ->  [  5  6  8 ]  ->  [ 1 5 13 2 6 14 4 8 16 ]
// [  9 10 11 12 ]      [ 13 14 16 ]
// [ 13 14 15 16 ]
std::array<float, 9> Mat4ToColumnMajorMat3Array(const glm::mat4& mat);

// Transforms a column major mat3 in array format to a glm::mat4.
// It is the inverse operation of Mat4ToColumnMajorMat3Array().
glm::mat4 ColumnMajorMat3ArrayToMat4(const std::array<float, 9>& matrix_array);

}  // namespace utils

#endif  // SRC_UI_SCENIC_LIB_UTILS_MATH_H_
