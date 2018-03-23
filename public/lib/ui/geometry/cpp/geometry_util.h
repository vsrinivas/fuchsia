// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_GEOMETRY_CPP_GEOMETRY_UTIL_H_
#define LIB_UI_GEOMETRY_CPP_GEOMETRY_UTIL_H_

#include <fuchsia/cpp/geometry.h>

namespace geometry {

inline bool operator==(const geometry::Rect& lhs, const geometry::Rect& rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width &&
         lhs.height == rhs.height;
}

inline bool operator!=(const geometry::Rect& lhs, const geometry::Rect& rhs) {
  return !(lhs == rhs);
}

inline bool operator==(const geometry::SizeF& lhs, const geometry::SizeF& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height;
}

inline bool operator!=(const geometry::SizeF& lhs, const geometry::SizeF& rhs) {
  return !(lhs == rhs);
}

inline bool operator==(const geometry::Size& lhs, const geometry::Size& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height;
}

inline bool operator!=(const geometry::Size& lhs, const geometry::Size& rhs) {
  return !(lhs == rhs);
}

inline bool operator==(const geometry::Point& lhs, const geometry::Point& rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y;
}

inline bool operator!=(const geometry::Point& lhs, const geometry::Point& rhs) {
  return !(lhs == rhs);
}

inline bool operator==(const geometry::Inset& lhs, const geometry::Inset& rhs) {
  return lhs.top == rhs.top && lhs.right == rhs.right &&
         lhs.bottom == rhs.bottom && lhs.left == rhs.left;
}

inline bool operator!=(const geometry::Inset& lhs, const geometry::Inset& rhs) {
  return !(lhs == rhs);
}

inline bool operator==(const geometry::InsetF& lhs,
                       const geometry::InsetF& rhs) {
  return lhs.top == rhs.top && lhs.right == rhs.right &&
         lhs.bottom == rhs.bottom && lhs.left == rhs.left;
}

inline bool operator!=(const geometry::InsetF& lhs,
                       const geometry::InsetF& rhs) {
  return !(lhs == rhs);
}

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
