// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene_manager/resources/shapes/planar_shape.h"

namespace mozart {
namespace scene {

class RectangleShape final : public PlanarShape {
 public:
  static const ResourceTypeInfo kTypeInfo;

  RectangleShape(Session* session,
                 ResourceId id,
                 float initial_width,
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

}  // namespace scene
}  // namespace mozart
