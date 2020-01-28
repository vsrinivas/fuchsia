// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_SHAPES_CIRCLE_SHAPE_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_SHAPES_CIRCLE_SHAPE_H_

#include "src/ui/scenic/lib/gfx/resources/shapes/planar_shape.h"

namespace scenic_impl {
namespace gfx {

class CircleShape final : public PlanarShape {
 public:
  static const ResourceTypeInfo kTypeInfo;

  CircleShape(Session* session, SessionId session_id, ResourceId id, float initial_radius);

  float radius() const { return radius_; }

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

  // |PlanarShape|.
  bool ContainsPoint(const escher::vec2& point) const override;

 private:
  float radius_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_SHAPES_CIRCLE_SHAPE_H_
