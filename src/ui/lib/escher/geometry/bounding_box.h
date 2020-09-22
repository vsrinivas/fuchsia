// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_GEOMETRY_BOUNDING_BOX_H_
#define SRC_UI_LIB_ESCHER_GEOMETRY_BOUNDING_BOX_H_

#include <vector>

#include "src/ui/lib/escher/geometry/types.h"

namespace escher {

class BoundingBox {
 public:
  // Non-empty box.  No error-checking; it is up to the caller to ensure that
  // all components of max are >= the corresponding component of min.
  BoundingBox(vec3 min, vec3 max);
  // Canonical representation of an empty box.
  //
  // "Empty" means "no point can inhabit this box". It does not mean "zero volume" or "zero area",
  // which still admits a singleton point inhabitant, or point inhabitants along a line.
  constexpr BoundingBox() : min_{1, 1, 1}, max_{0, 0, 0} {}
  // Return an empty box if max < min along any of the coordinate axes,
  // or if max == min along more than |max_degenerate_dimensions| of the
  // coordinate axes.  Otherwise return a non-empty box.
  static BoundingBox NewChecked(vec3 min, vec3 max, uint32_t max_degenerate_dimensions = 0);

  const vec3& min() const { return min_; }
  const vec3& max() const { return max_; }

  bool operator==(const BoundingBox& box) const { return min_ == box.min_ && max_ == box.max_; }
  bool operator!=(const BoundingBox& box) const { return !(*this == box); }

  // Expand this bounding box to encompass the other.  Return this box.
  BoundingBox& Join(const BoundingBox& box);

  // Shrink this box to be the intersection of this with the other.  If the
  // boxes do not intersect, this box becomes empty.  Return this box.
  BoundingBox& Intersect(const BoundingBox& box);

  float width() const { return max_.x - min_.x; }
  float height() const { return max_.y - min_.y; }
  float depth() const { return max_.z - min_.z; }

  vec3 extent() const { return vec3(width(), height(), depth()); }

  // Return true if the other box is completely contained by this one.
  bool Contains(const BoundingBox& box) const {
    // We don't need to check if this box is empty, because the way we define
    // an empty box guarantees that the subsequent tests can't pass.
    return glm::all(glm::lessThanEqual(min_, box.min_)) &&
           glm::all(glm::greaterThanEqual(max_, box.max_)) && !box.is_empty();
  }

  bool Contains(const vec4& point) const {
    if (point.x < min_.x || point.x > max_.x)
      return false;
    if (point.y < min_.y || point.y > max_.y)
      return false;
    if (point.z < min_.z || point.z > max_.z)
      return false;
    return true;
  }

  // See definition of "empty box" in the default constructor.
  bool is_empty() const { return *this == BoundingBox(); }

  // Return the number of bounding box corners that are clipped by the
  // specified plane (between 0 and 8).  Since this is a 2D plane, the z
  // coordinate is ignored, and only 4 corners need to be tested.
  uint32_t NumClippedCorners(const plane2& plane, const float_t& epsilon = kEpsilon) const;

  // Return the number of bounding box corners that are clipped by the
  // specified plane (between 0 and 8).
  uint32_t NumClippedCorners(const plane3& plane, const float_t& epsilon = kEpsilon) const;

  std::vector<plane3> CreatePlanes() const;

  // Generates a matrix based on the min/max value of the current bounding box that would,
  // if applied to a unit cube, scale/translate that cube to be the exact size and shape
  // of the existing bounding box.
  mat4 CreateTransform() const;

 private:
  vec3 min_;
  vec3 max_;
};

// Return a new BoundingBox that encloses the 8 corners of this box, after
// they are transformed by the matrix.  Note: this can cause the box to grow,
// e.g. if you rotate it by 45 degrees.
BoundingBox operator*(const mat4& matrix, const BoundingBox& box);

// Return a new Bounding box by translating the input box.
inline BoundingBox operator+(const vec3& translation, const BoundingBox& box) {
  return box.is_empty() ? BoundingBox()
                        : BoundingBox(box.min() + translation, box.max() + translation);
}

// Return a new Bounding box by translating the input box.
inline BoundingBox operator+(const BoundingBox& box, const vec3& translation) {
  return translation + box;
}

// Debugging.
ESCHER_DEBUG_PRINTABLE(BoundingBox);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_GEOMETRY_BOUNDING_BOX_H_
