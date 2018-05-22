// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_SHAPES_RECTANGLE_SHAPE_H_
#define GARNET_LIB_UI_GFX_RESOURCES_SHAPES_RECTANGLE_SHAPE_H_

#include "garnet/lib/ui/gfx/resources/shapes/planar_shape.h"

namespace scenic {
namespace gfx {

class RectangleShape final : public PlanarShape {
 public:
  static const ResourceTypeInfo kTypeInfo;

  RectangleShape(Session* session, scenic::ResourceId id, float initial_width,
                 float initial_height);

  float width() const { return width_; }
  float height() const { return height_; }

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

  // |PlanarShape|.
  bool ContainsPoint(const escher::vec2& point) const override;

  // |Shape|.
  escher::Object GenerateRenderObject(
      const escher::mat4& transform,
      const escher::MaterialPtr& material) override;

 private:
  float width_;
  float height_;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_SHAPES_RECTANGLE_SHAPE_H_
