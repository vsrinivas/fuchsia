// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_LIGHTS_DIRECTIONAL_LIGHT_H_
#define GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_LIGHTS_DIRECTIONAL_LIGHT_H_

#include "garnet/bin/ui/scene_manager/resources/lights/light.h"

namespace scene_manager {

class DirectionalLight final : public Light {
 public:
  static const ResourceTypeInfo kTypeInfo;

  DirectionalLight(Session* session, scenic::ResourceId id);

  // The direction will be normalized before storing.  Returns false if the
  // length of |direction| is nearly zero.
  bool SetDirection(const glm::vec3& direction);

  // The normalized direction of the light source.
  const glm::vec3& direction() const { return direction_; }

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

 private:
  glm::vec3 direction_ = {0.f, 0.f, -1.f};
};

}  // namespace scene_manager

#endif  // GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_LIGHTS_DIRECTIONAL_LIGHT_H_
