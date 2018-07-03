// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_CAMERA_H_
#define GARNET_LIB_UI_GFX_RESOURCES_CAMERA_H_

#include "garnet/lib/ui/gfx/resources/buffer.h"
#include "garnet/lib/ui/gfx/resources/nodes/scene.h"
#include "garnet/lib/ui/gfx/resources/resource.h"

#include "lib/escher/scene/camera.h"
#include "lib/escher/scene/stage.h"

namespace scenic {
namespace gfx {

class Camera : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Camera(Session* session, scenic::ResourceId id, ScenePtr scene);
  virtual ~Camera() {}

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

  const ScenePtr& scene() const { return scene_; }

  void SetTransform(const glm::vec3& eye_position, const glm::vec3& eye_look_at,
                    const glm::vec3& eye_up);

  void SetProjection(const float fovy);

  // Sets the buffer for this camera. For details see SetCameraPoseBufferCmd
  // in //garnet/public/fidl/fuchsia.ui.gfx/commands.fidl
  void SetPoseBuffer(fxl::RefPtr<Buffer> buffer, uint32_t num_entries,
                     uint64_t base_time, uint64_t time_interval);

  const glm::vec3& eye_position() const { return eye_position_; }
  const glm::vec3& eye_look_at() const { return eye_look_at_; }
  const glm::vec3& eye_up() const { return eye_up_; }
  float fovy() const { return fovy_; }

  escher::Camera GetEscherCamera(const escher::ViewingVolume& volume) const;

  // Projects the provided |ray| into the coordinate space of this camera's
  // scene.
  //
  // The first entry in the pair is the projected ray, and the second entry
  // is the transformation that was applied to the passed in ray.
  std::pair<escher::ray4, escher::mat4> ProjectRayIntoScene(
      const escher::ray4& ray,
      const escher::ViewingVolume& viewing_volume) const;

 protected:
  // Note: StereoCamera subclasses Camera and provides its own ResourceTypeInfo.
  Camera(Session* session, scenic::ResourceId id, ScenePtr scene,
         const ResourceTypeInfo& type_info);

  ScenePtr scene_;

  glm::vec3 eye_position_ = glm::vec3();
  glm::vec3 eye_look_at_ = glm::vec3();
  glm::vec3 eye_up_ = glm::vec3(0.0f, 1.0f, 0.0f);
  float fovy_ = 0;

  // PoseBuffer parameters
  fxl::RefPtr<Buffer> pose_buffer_;
  uint32_t num_entries_ = 0;
  uint64_t base_time_ = 0;
  uint64_t time_interval_ = 0;
};

using CameraPtr = fxl::RefPtr<Camera>;

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_CAMERA_H_
