// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/stereo_camera.h"

namespace scenic {
namespace gfx {

const ResourceTypeInfo StereoCamera::kTypeInfo = {
    ResourceType::kCamera | ResourceType::kStereoCamera, "StereoCamera"};

StereoCamera::StereoCamera(Session* session, scenic::ResourceId id,
                           ScenePtr scene)
    : Camera(session, id, scene, StereoCamera::kTypeInfo) {
  viewports_[Eye::LEFT] = {0.f, 0.f, 0.5f, 1.f};
  viewports_[Eye::RIGHT] = {0.5f, 0.f, 0.5f, 1.f};
}

void StereoCamera::SetStereoProjection(const glm::mat4 left_projection,
                                       const glm::mat4 right_projection) {
  projection_[Eye::LEFT] = left_projection;
  projection_[Eye::RIGHT] = right_projection;
}

escher::Camera StereoCamera::GetEscherCamera(Eye eye) const {
  escher::Camera camera(glm::lookAt(eye_position(), eye_look_at(), eye_up()),
                        projection_[eye]);

  camera.SetViewport(viewports_[eye]);

  if (pose_buffer_) {
    escher::hmd::PoseBuffer escher_pose_buffer(pose_buffer_->escher_buffer(),
                                               num_entries_, base_time_,
                                               time_interval_);
    camera.SetPoseBuffer(escher_pose_buffer);
  }

  return camera;
}

}  // namespace gfx
}  // namespace scenic
