// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_CAMERA_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_CAMERA_H_

#include "src/ui/lib/escher/scene/camera.h"
#include "src/ui/scenic/lib/gfx/resources/buffer.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/scene.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"

namespace scenic_impl {
namespace gfx {

class Camera : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Camera(Session* session, SessionId session_id, ResourceId id, ScenePtr scene);
  virtual ~Camera() {}

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

  const ScenePtr& scene() const { return scene_; }

  void SetTransform(const glm::vec3& eye_position, const glm::vec3& eye_look_at,
                    const glm::vec3& eye_up);

  void SetProjection(const float fovy);

  void SetClipSpaceTransform(const glm::vec2& translation, float scale);

  // Sets the buffer for this camera. For details see SetCameraPoseBufferCmd
  // in //sdk/fidl/fuchsia.ui.gfx/commands.fidl
  void SetPoseBuffer(fxl::RefPtr<Buffer> buffer, uint32_t num_entries, zx::time base_time,
                     zx::duration time_interval);

  const glm::vec3& eye_position() const { return eye_position_; }
  const glm::vec3& eye_look_at() const { return eye_look_at_; }
  const glm::vec3& eye_up() const { return eye_up_; }
  float fovy() const { return fovy_; }

  escher::Camera GetEscherCamera(const escher::ViewingVolume& volume) const;

  escher::hmd::PoseBuffer GetEscherPoseBuffer() const;

  // Projects the provided |ray| into global coordinates.
  //
  // The first entry in the pair is the projected ray, and the second entry
  // is the transformation that was applied to the passed in ray.
  std::pair<escher::ray4, escher::mat4> ProjectRay(
      const escher::ray4& ray, const escher::ViewingVolume& viewing_volume) const;

 protected:
  // Note: StereoCamera subclasses Camera and provides its own ResourceTypeInfo.
  Camera(Session* session, SessionId session_id, ResourceId id, ScenePtr scene,
         const ResourceTypeInfo& type_info);

  ScenePtr scene_;

  glm::vec3 eye_position_ = glm::vec3();
  glm::vec3 eye_look_at_ = glm::vec3();
  glm::vec3 eye_up_ = glm::vec3(0.0f, 1.0f, 0.0f);
  float fovy_ = 0;
  bool has_clip_space_transform_ = false;
  glm::mat4 clip_space_transform_;

  // PoseBuffer parameters
  fxl::RefPtr<Buffer> pose_buffer_;
  uint32_t num_entries_ = 0;
  zx::time base_time_ = zx::time(0);
  zx::duration time_interval_ = zx::duration(0);
};

using CameraPtr = fxl::RefPtr<Camera>;

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_CAMERA_H_
