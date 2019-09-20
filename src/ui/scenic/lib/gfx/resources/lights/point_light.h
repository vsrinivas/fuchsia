// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_LIGHTS_POINT_LIGHT_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_LIGHTS_POINT_LIGHT_H_

#include "src/ui/scenic/lib/gfx/resources/lights/light.h"

namespace scenic_impl {
namespace gfx {

class PointLight final : public Light {
 public:
  static const ResourceTypeInfo kTypeInfo;

  PointLight(Session* session, SessionId session_id, ResourceId id);

  bool SetPosition(const glm::vec3& position);
  const glm::vec3& position() const { return position_; }

  // See Escher's paper_light.h and SetPointLightFalloffCmd in fuchsia.ui.gfx.
  bool SetFalloff(float falloff);
  float falloff() const { return falloff_; }

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

 private:
  glm::vec3 position_ = {0.f, 0.f, -1.f};
  float falloff_ = 1.f;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_LIGHTS_POINT_LIGHT_H_
