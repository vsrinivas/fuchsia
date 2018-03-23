// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/geometry/cpp/geometry_util.h"

#include <string.h>

#include <limits>

#include "lib/fxl/logging.h"

namespace geometry {

static const float kIdentityMatrix[]{
    1.f, 0.f, 0.f, 0.f,  // comments to prevent
    0.f, 1.f, 0.f, 0.f,  // auto formatter reflow
    0.f, 0.f, 1.f, 0.f,  //
    0.f, 0.f, 0.f, 1.f};

void SetIdentityTransform(geometry::Transform* transform) {
  FXL_DCHECK(transform->matrix.count() == 16);
  memcpy(static_cast<void*>(transform->matrix.mutable_data()), kIdentityMatrix,
         sizeof(kIdentityMatrix));
}

void SetTranslationTransform(geometry::Transform* transform,
                             float x,
                             float y,
                             float z) {
  SetIdentityTransform(transform);
  Translate(transform, x, y, z);
}

void SetScaleTransform(geometry::Transform* transform,
                       float x,
                       float y,
                       float z) {
  SetIdentityTransform(transform);
  Scale(transform, x, y, z);
}

void Translate(geometry::Transform* transform, float x, float y, float z) {
  transform->matrix.at(3) += x;
  transform->matrix.at(7) += y;
  transform->matrix.at(11) += z;
}

void Scale(geometry::Transform* transform, float x, float y, float z) {
  transform->matrix.at(0) *= x;
  transform->matrix.at(5) *= y;
  transform->matrix.at(10) *= z;
}

geometry::TransformPtr CreateIdentityTransform() {
  geometry::TransformPtr result = geometry::Transform::New();
  result->matrix = fidl::Array<float, 16>();
  SetIdentityTransform(result.get());
  return result;
}

geometry::TransformPtr CreateTranslationTransform(float x, float y, float z) {
  return Translate(CreateIdentityTransform(), x, y, z);
}

geometry::TransformPtr CreateScaleTransform(float x, float y, float z) {
  return Scale(CreateIdentityTransform(), x, y, z);
}

geometry::TransformPtr Translate(geometry::TransformPtr transform,
                                 float x,
                                 float y,
                                 float z) {
  Translate(transform.get(), x, y, z);
  return transform;
}

geometry::TransformPtr Scale(geometry::TransformPtr transform,
                             float x,
                             float y,
                             float z) {
  Scale(transform.get(), x, y, z);
  return transform;
}

geometry::PointF TransformPoint(const geometry::Transform& transform,
                                const geometry::PointF& point) {
  geometry::PointF result;
  const auto& m = transform.matrix;
  float w = m[12] * point.x + m[13] * point.y + m[15];
  if (w) {
    w = 1.f / w;
    result.x = (m[0] * point.x + m[1] * point.y + m[3]) * w;
    result.y = (m[4] * point.x + m[5] * point.y + m[7]) * w;
  } else {
    result.x = std::numeric_limits<float>::infinity();
    result.y = std::numeric_limits<float>::infinity();
  }
  return result;
}

}  // namespace mozart
