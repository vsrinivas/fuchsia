// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/geometry/cpp/formatting.h"

#include <zircon/assert.h>

#include <ostream>

namespace fuchsia {
namespace math {

std::ostream& operator<<(std::ostream& os, const Point& value) {
  return os << "{x=" << value.x << ", y=" << value.y << "}";
}

std::ostream& operator<<(std::ostream& os, const PointF& value) {
  return os << "{x=" << value.x << ", y=" << value.y << "}";
}

std::ostream& operator<<(std::ostream& os, const Point3F& value) {
  return os << "{x=" << value.x << ", y=" << value.y << ", z=" << value.z
            << "}";
}

std::ostream& operator<<(std::ostream& os, const Rect& value) {
  return os << "{x=" << value.x << ", y=" << value.y
            << ", width=" << value.width << ", height=" << value.height << "}";
}

std::ostream& operator<<(std::ostream& os, const RectF& value) {
  return os << "{x=" << value.x << ", y=" << value.y
            << ", width=" << value.width << ", height=" << value.height << "}";
}

std::ostream& operator<<(std::ostream& os, const RRectF& value) {
  return os << "{x=" << value.x << ", y=" << value.y
            << ", width=" << value.width << ", height=" << value.height
            << ", top_left_radius_x=" << value.top_left_radius_x
            << ", top_left_radius_y=" << value.top_left_radius_y
            << ", top_right_radius_x=" << value.top_right_radius_x
            << ", top_right_radius_y=" << value.top_right_radius_y
            << ", bottom_left_radius_x=" << value.bottom_left_radius_x
            << ", bottom_left_radius_y=" << value.bottom_left_radius_y
            << ", bottom_right_radius_x=" << value.bottom_right_radius_x
            << ", bottom_right_radius_y=" << value.bottom_right_radius_y << "}";
}

std::ostream& operator<<(std::ostream& os, const Size& value) {
  return os << "{width=" << value.width << ", height=" << value.height << "}";
}

std::ostream& operator<<(std::ostream& os, const SizeF& value) {
  return os << "{width=" << value.width << ", height=" << value.height << "}";
}

std::ostream& operator<<(std::ostream& os, const Inset& value) {
  return os << "{left=" << value.left << ", top=" << value.top
            << ", right=" << value.right << ", bottom=" << value.bottom << "}";
}

std::ostream& operator<<(std::ostream& os, const InsetF& value) {
  return os << "{left=" << value.left << ", top=" << value.top
            << ", right=" << value.right << ", bottom=" << value.bottom << "}";
}

std::ostream& operator<<(std::ostream& os, const Transform& value) {
  ZX_DEBUG_ASSERT(value.matrix.count() == 16);
  os << "[";
  for (size_t i = 0; i < 4; i++) {
    if (i != 0)
      os << ", ";
    os << "[";
    for (size_t j = 0; j < 4; j++) {
      if (j != 0)
        os << ", ";
      os << value.matrix.at(i * 4 + j);
    }
    os << "]";
  }
  os << "]";
  return os;
}

}  // namespace math
}  // namespace fuchsia
