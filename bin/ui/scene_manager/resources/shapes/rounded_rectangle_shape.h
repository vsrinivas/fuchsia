// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/resources/shapes/planar_shape.h"

#include "lib/escher/forward_declarations.h"
#include "lib/escher/shape/rounded_rect.h"

namespace scene_manager {

class RoundedRectangleShape final : public PlanarShape {
 public:
  static const ResourceTypeInfo kTypeInfo;

  RoundedRectangleShape(Session* session,
                        scenic::ResourceId id,
                        const escher::RoundedRectSpec& spec,
                        escher::MeshPtr mesh);

  float width() const { return spec_.width; }
  float height() const { return spec_.height; }
  float top_left_radius() const { return spec_.top_left_radius; }
  float top_right_radius() const { return spec_.top_right_radius; }
  float bottom_right_radius() const { return spec_.bottom_right_radius; }
  float bottom_left_radius() const { return spec_.bottom_left_radius; }

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

  // |PlanarShape|.
  bool ContainsPoint(const escher::vec2& point) const override;

  // |Shape|.
  escher::Object GenerateRenderObject(
      const escher::mat4& transform,
      const escher::MaterialPtr& material) override;

 private:
  escher::RoundedRectSpec spec_;
  escher::MeshPtr mesh_;
};

}  // namespace scene_manager
