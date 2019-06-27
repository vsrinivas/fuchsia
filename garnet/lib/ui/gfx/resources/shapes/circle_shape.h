// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_SHAPES_CIRCLE_SHAPE_H_
#define GARNET_LIB_UI_GFX_RESOURCES_SHAPES_CIRCLE_SHAPE_H_

#include "garnet/lib/ui/gfx/resources/shapes/planar_shape.h"

namespace scenic_impl {
namespace gfx {

class CircleShape final : public PlanarShape {
 public:
  static const ResourceTypeInfo kTypeInfo;

  CircleShape(Session* session, ResourceId id, float initial_radius);

  float radius() const { return radius_; }

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

  // |PlanarShape|.
  bool ContainsPoint(const escher::vec2& point) const override;

  // |Shape|.
  escher::Object GenerateRenderObject(const escher::mat4& transform,
                                      const escher::MaterialPtr& material) override;

 private:
  float radius_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_SHAPES_CIRCLE_SHAPE_H_
