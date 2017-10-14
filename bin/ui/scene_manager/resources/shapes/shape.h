// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/resources/resource.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/scene/object.h"

namespace scene_manager {

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
  virtual bool GetIntersection(const escher::ray4& ray,
                               float* out_distance) const = 0;

  // Generate an object to add to an escher::Model.
  virtual escher::Object GenerateRenderObject(
      const escher::mat4& transform,
      const escher::MaterialPtr& material) = 0;

 protected:
  Shape(Session* session,
        scenic::ResourceId id,
        const ResourceTypeInfo& type_info);
};

using ShapePtr = fxl::RefPtr<Shape>;

}  // namespace scene_manager
