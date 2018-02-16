// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/forward_declarations.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/hmd/pose_buffer.h"

namespace escher {

// Generates and encapsulates a view/projection matrix pair.  The camera follows
// the Vulkan convention of looking down the negative Z-axis.
class Camera {
 public:
  Camera(const mat4& transform, const mat4& projection);

  // Create an camera in the default position for a full-screen orthographic
  // projection.
  static Camera NewOrtho(const ViewingVolume& volume);

  // Create an orthographic camera looking at the viewing volume in the
  // specified direction.
  static Camera NewForDirectionalShadowMap(const ViewingVolume& volume,
                                           const glm::vec3& direction);

  // Create a camera with a perspective projection.
  static Camera NewPerspective(const ViewingVolume& volume,
                               const mat4& transform,
                               float fovy);

  const mat4& transform() const { return transform_; }
  const mat4& projection() const { return projection_; }

  void SetPoseBuffer(const hmd::PoseBuffer& pose_buffer) {
    pose_buffer_ = pose_buffer;
  }
  const hmd::PoseBuffer& pose_buffer() const { return pose_buffer_; }

  void SetLatchedPoseBuffer(const BufferPtr& latched_pose_buffer) {
    latched_pose_buffer_ = latched_pose_buffer;
  }
  const BufferPtr& latched_pose_buffer() const { return latched_pose_buffer_; }

 private:
  mat4 transform_;
  mat4 projection_;
  hmd::PoseBuffer pose_buffer_;

  // Contains the latched pose and vp matrices latched out of pose_buffer_.
  // See pose_buffer_latching_shader.h for details on buffer layout.
  BufferPtr latched_pose_buffer_;
};

// Debugging.
ESCHER_DEBUG_PRINTABLE(Camera);

}  // namespace escher
