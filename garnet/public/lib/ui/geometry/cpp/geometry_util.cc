// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/geometry/cpp/geometry_util.h"

#include <string.h>
#include <zircon/assert.h>

#include <limits>

namespace fuchsia {
namespace math {

static const float kIdentityMatrix[]{
    1.f, 0.f, 0.f, 0.f,  // comments to prevent
    0.f, 1.f, 0.f, 0.f,  // auto formatter reflow
    0.f, 0.f, 1.f, 0.f,  //
    0.f, 0.f, 0.f, 1.f};

void SetIdentityTransform(fuchsia::math::Transform* transform) {
  ZX_DEBUG_ASSERT(transform->matrix.count() == 16);
  memcpy(static_cast<void*>(transform->matrix.mutable_data()), kIdentityMatrix,
         sizeof(kIdentityMatrix));
}

void SetTranslationTransform(fuchsia::math::Transform* transform, float x,
                             float y, float z) {
  SetIdentityTransform(transform);
  Translate(transform, x, y, z);
}

void SetScaleTransform(fuchsia::math::Transform* transform, float x, float y,
                       float z) {
  SetIdentityTransform(transform);
  Scale(transform, x, y, z);
}

void Translate(fuchsia::math::Transform* transform, float x, float y, float z) {
  transform->matrix.at(3) += x;
  transform->matrix.at(7) += y;
  transform->matrix.at(11) += z;
}

void Scale(fuchsia::math::Transform* transform, float x, float y, float z) {
  transform->matrix.at(0) *= x;
  transform->matrix.at(5) *= y;
  transform->matrix.at(10) *= z;
}

fuchsia::math::TransformPtr CreateIdentityTransform() {
  fuchsia::math::TransformPtr result = fuchsia::math::Transform::New();
  result->matrix = fidl::Array<float, 16>();
  SetIdentityTransform(result.get());
  return result;
}

fuchsia::math::TransformPtr CreateTranslationTransform(float x, float y,
                                                       float z) {
  return Translate(CreateIdentityTransform(), x, y, z);
}

fuchsia::math::TransformPtr CreateScaleTransform(float x, float y, float z) {
  return Scale(CreateIdentityTransform(), x, y, z);
}

fuchsia::math::TransformPtr Translate(fuchsia::math::TransformPtr transform,
                                      float x, float y, float z) {
  Translate(transform.get(), x, y, z);
  return transform;
}

fuchsia::math::TransformPtr Scale(fuchsia::math::TransformPtr transform,
                                  float x, float y, float z) {
  Scale(transform.get(), x, y, z);
  return transform;
}

fuchsia::math::PointF TransformPoint(const fuchsia::math::Transform& transform,
                                     const fuchsia::math::PointF& point) {
  fuchsia::math::PointF result;
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

}  // namespace math
}  // namespace fuchsia
