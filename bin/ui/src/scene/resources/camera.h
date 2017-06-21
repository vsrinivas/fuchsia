// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene/resources/nodes/scene.h"
#include "apps/mozart/src/scene/resources/resource.h"

namespace mozart {
namespace scene {

class Camera final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Camera(Session* session, ResourceId id, ScenePtr scene);

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

  const ScenePtr& scene() const { return scene_; }

  void SetProjectionMatrix(const escher::mat4& matrix);

 private:
  ScenePtr scene_;
  escher::mat4 projection_matrix_;
};

using CameraPtr = ftl::RefPtr<Camera>;

}  // namespace scene
}  // namespace mozart
