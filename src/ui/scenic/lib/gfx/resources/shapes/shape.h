// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_SHAPES_SHAPE_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_SHAPES_SHAPE_H_

#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/scene/object.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"

namespace scenic_impl {
namespace gfx {

class Shape : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // Computes the closest point of intersection between the ray's origin
  // and the front side of the shape.
  //
  // |out_distance| is set to the distance from the ray's origin to the
  // closest point of intersection in multiples of the ray's direction vector.
  //
  // Returns true if there is an intersection, otherwise returns false and
  // leaves |out_distance| unmodified.
  virtual bool GetIntersection(const escher::ray4& ray, float* out_distance) const = 0;

 protected:
  Shape(Session* session, SessionId session_id, ResourceId id, const ResourceTypeInfo& type_info);
};

using ShapePtr = fxl::RefPtr<Shape>;

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_SHAPES_SHAPE_H_
