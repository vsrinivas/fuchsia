// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/resources/shapes/shape.h"

namespace scene_manager {

// A shape that lies within the Z=0 plane of the local coordinate system.
// As a result, |GetIntersection()| is implemented by intersecting a ray
// with this plane and calling |ContainsPoint()| on the result.
class PlanarShape : public Shape {
 public:
  // |Shape|
  bool GetIntersection(const escher::ray4& ray,
                       float* out_distance) const override;

  // Returns if the given point lies within its bounds of this shape.
  virtual bool ContainsPoint(const escher::vec2& point) const = 0;

 protected:
  PlanarShape(Session* session,
              scenic::ResourceId id,
              const ResourceTypeInfo& type_info);
};

}  // namespace scene_manager
