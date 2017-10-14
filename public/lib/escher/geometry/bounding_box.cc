// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/geometry/bounding_box.h"

#include "lib/fxl/logging.h"

namespace escher {

BoundingBox::BoundingBox(vec3 min, vec3 max) : min_(min), max_(max) {
#ifndef NDEBUG
  FXL_DCHECK(min.x <= max.x) << min << " " << max;
  FXL_DCHECK(min.y <= max.y) << min << " " << max;
  FXL_DCHECK(min.z <= max.z) << min << " " << max;
  int dimensions = (min.x != max.x ? 1 : 0) + (min.y != max.y ? 1 : 0) +
                   (min.z != max.z ? 1 : 0);
  // Should use empty bounding-box if box is 1D or 0D.
  FXL_DCHECK(dimensions >= 2);
#endif
}

BoundingBox& BoundingBox::Join(const BoundingBox& box) {
  if (is_empty()) {
    min_ = box.min_;
    max_ = box.max_;
  } else if (!box.is_empty()) {
    min_ = glm::min(min_, box.min_);
    max_ = glm::max(max_, box.max_);
  }
  return *this;
}

BoundingBox& BoundingBox::Intersect(const BoundingBox& box) {
  if (is_empty()) {
    return *this;
  } else if (box.is_empty()) {
    *this = BoundingBox();
  } else {
    min_ = glm::max(min_, box.min_);
    max_ = glm::min(max_, box.max_);
    if (min_.x > max_.x || min_.y > max_.y || min_.z > max_.z) {
      *this = BoundingBox();
    } else {
      int dimensions = (min_.x != max_.x ? 1 : 0) + (min_.y != max_.y ? 1 : 0) +
                       (min_.z != max_.z ? 1 : 0);
      if (dimensions < 2) {
        // We consider the intersection between boxes that touch at only one
        // point or an edge to be empty.
        *this = BoundingBox();
      }
    }
  }
  return *this;
}

BoundingBox operator*(const mat4& matrix, const BoundingBox& box) {
  FXL_DCHECK(matrix[3][3] == 1.f);  // No perspective allowed.

  if (box.is_empty()) {
    return box;
  }

  // Fancy trick to transform an AABB.
  // See http://dev.theomader.com/transform-bounding-boxes/
  vec4 xa = box.min().x * matrix[0];
  vec4 xb = box.max().x * matrix[0];
  vec4 ya = box.min().y * matrix[1];
  vec4 yb = box.max().y * matrix[1];
  vec4 za = box.min().z * matrix[2];
  vec4 zb = box.max().z * matrix[2];
  vec3 min = glm::min(vec3(xa), vec3(xb)) + glm::min(vec3(ya), vec3(yb)) +
             glm::min(vec3(za), vec3(zb)) + vec3(matrix[3]);
  vec3 max = glm::max(vec3(xa), vec3(xb)) + glm::max(vec3(ya), vec3(yb)) +
             glm::max(vec3(za), vec3(zb)) + vec3(matrix[3]);

  return BoundingBox(min, max);
}

}  // namespace escher
