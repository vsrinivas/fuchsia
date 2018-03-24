// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_STEREO_CAMERA_H_
#define GARNET_LIB_UI_GFX_RESOURCES_STEREO_CAMERA_H_

#include "garnet/lib/ui/gfx/resources/camera.h"

namespace scenic {
namespace gfx {

class StereoCamera final : public Camera {
 public:
  static const ResourceTypeInfo kTypeInfo;

  StereoCamera(Session* session, scenic::ResourceId id, ScenePtr scene);

  void SetStereoProjection(const glm::mat4 left_projection,
                           const glm::mat4 right_projection);

  enum Eye {
    LEFT = 0,
    RIGHT = 1,
  };

  escher::Camera GetEscherCamera(Eye eye) const;

 private:
  ScenePtr scene_;
  // Projection Matrices for each eye, left at position 0, right at position 1.
  glm::mat4 projection_[2];
  escher::Camera::Viewport viewports_[2];
};

using StereoCameraPtr = fxl::RefPtr<StereoCamera>;

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_STEREO_CAMERA_H_
