// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/resources/nodes/scene.h"
#include "garnet/bin/ui/scene_manager/resources/resource.h"

#include "escher/scene/camera.h"

namespace scene_manager {

class Camera final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Camera(Session* session, scenic::ResourceId id, ScenePtr scene);

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

  const ScenePtr& scene() const { return scene_; }

  void SetProjection(const glm::vec3& eye_position,
                     const glm::vec3& eye_look_at,
                     const glm::vec3& eye_up,
                     float fovy);

  const glm::vec3& eye_position() const { return eye_position_; }
  const glm::vec3& eye_look_at() const { return eye_look_at_; }
  const glm::vec3& eye_up() const { return eye_up_; }
  float fovy() const { return fovy_; }

  escher::Camera GetEscherCamera(const escher::ViewingVolume& volume) const;

 private:
  ScenePtr scene_;

  glm::vec3 eye_position_;
  glm::vec3 eye_look_at_;
  glm::vec3 eye_up_;
  float fovy_ = 0;
};

using CameraPtr = fxl::RefPtr<Camera>;

}  // namespace scene_manager
