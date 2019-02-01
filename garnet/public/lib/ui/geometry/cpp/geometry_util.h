// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_GEOMETRY_CPP_GEOMETRY_UTIL_H_
#define LIB_UI_GEOMETRY_CPP_GEOMETRY_UTIL_H_

#include <fuchsia/math/cpp/fidl.h>

namespace fuchsia {
namespace math {

void SetIdentityTransform(fuchsia::math::Transform* transform);
void SetTranslationTransform(fuchsia::math::Transform* transform, float x,
                             float y, float z = 0.0f);
void SetScaleTransform(fuchsia::math::Transform* transform, float x, float y,
                       float z = 1.0f);

void Translate(fuchsia::math::Transform* transform, float x, float y,
               float z = 0.0f);
void Scale(fuchsia::math::Transform* transform, float x, float y,
           float z = 1.0f);

fuchsia::math::TransformPtr CreateIdentityTransform();
fuchsia::math::TransformPtr CreateTranslationTransform(float x, float y,
                                                       float z = 0.0f);
fuchsia::math::TransformPtr CreateScaleTransform(float x, float y,
                                                 float z = 1.0f);

fuchsia::math::TransformPtr Translate(fuchsia::math::TransformPtr transform,
                                      float x, float y, float z = 0.0f);
fuchsia::math::TransformPtr Scale(fuchsia::math::TransformPtr transform,
                                  float x, float y, float z = 1.0f);

fuchsia::math::PointF TransformPoint(const fuchsia::math::Transform& transform,
                                     const fuchsia::math::PointF& point);

}  // namespace math
}  // namespace fuchsia

#endif  // LIB_UI_GEOMETRY_CPP_GEOMETRY_UTIL_H_
