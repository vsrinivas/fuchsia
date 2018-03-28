// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_GEOMETRY_CPP_GEOMETRY_UTIL_H_
#define LIB_UI_GEOMETRY_CPP_GEOMETRY_UTIL_H_

#include <fuchsia/cpp/geometry.h>

namespace geometry {

void SetIdentityTransform(geometry::Transform* transform);
void SetTranslationTransform(geometry::Transform* transform,
                             float x,
                             float y,
                             float z = 0.0f);
void SetScaleTransform(geometry::Transform* transform,
                       float x,
                       float y,
                       float z = 1.0f);

void Translate(geometry::Transform* transform,
               float x,
               float y,
               float z = 0.0f);
void Scale(geometry::Transform* transform, float x, float y, float z = 1.0f);

geometry::TransformPtr CreateIdentityTransform();
geometry::TransformPtr CreateTranslationTransform(float x,
                                                  float y,
                                                  float z = 0.0f);
geometry::TransformPtr CreateScaleTransform(float x, float y, float z = 1.0f);

geometry::TransformPtr Translate(geometry::TransformPtr transform,
                                 float x,
                                 float y,
                                 float z = 0.0f);
geometry::TransformPtr Scale(geometry::TransformPtr transform,
                             float x,
                             float y,
                             float z = 1.0f);

geometry::PointF TransformPoint(const geometry::Transform& transform,
                                const geometry::PointF& point);

}  // namespace mozart

#endif  // LIB_UI_GEOMETRY_CPP_GEOMETRY_UTIL_H_
