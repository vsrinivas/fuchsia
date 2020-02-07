// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_HMD_POSE_BUFFER_LATCHING_SHADER_H_
#define SRC_UI_LIB_ESCHER_HMD_POSE_BUFFER_LATCHING_SHADER_H_

#include "src/ui/lib/escher/hmd/pose_buffer.h"
#include "src/ui/lib/escher/impl/compute_shader.h"

namespace escher {
namespace hmd {

extern const char* kPoseLatchingShaderName;
extern const char* g_kernel_src;

// Uses a simple compute shader to latch a pose out of the pose buffer.
// public/fidl/fuchsia.ui.gfx/commands.fidl for details on pose buffer.
class PoseBufferLatchingShader {
 public:
  PoseBufferLatchingShader(EscherWeakPtr escher);

  // Latches a pose from the pose buffer for |latch_time|.
  // The returned buffer will contain the raw latched pose as well as a
  // ViewProjection matrix computed from |pose_buffer| and |camera| as
  // camera->transform() * mat4(latched_pose) * camera->projection().
  // These output values will be layed out in the output buffer as follows:
  //
  // struct OutputBuffer {
  //   struct Pose latched_pose;
  //   mat4  vp_matrix;
  // }
  //
  // Note that this is a convenience entry point which simply calls through
  // to LatchStereoPose.
  //
  // For details on pose buffers and the layout of the Pose struct see
  // //sdk/fidl/fuchsia.ui.gfx/commands.fidl
  BufferPtr LatchPose(const FramePtr& frame, const Camera& camera, PoseBuffer pose_buffer,
                      int64_t latch_time, bool host_accessible_output = false);

  // The same as LatchPose but takes two cameras and computes a ViewProjection
  // matrix for each.
  // These output values will be layed out in the output buffer as follows:
  //
  // struct OutputBuffer {
  //   struct Pose latched_pose;
  //   mat4  left_vp_matrix;
  //   mat4  right_vp_matrix;
  // }
  BufferPtr LatchStereoPose(const FramePtr& frame, const Camera& left_camera,
                            const Camera& right_camera, PoseBuffer pose_buffer, int64_t latch_time,
                            bool host_accessible_output = false);

  static constexpr uint32_t kLeftVpMatrixOffset = sizeof(Pose);
  static constexpr uint32_t kRightVpMatrixOffset = kLeftVpMatrixOffset + 16 * sizeof(float);

 private:
  const EscherWeakPtr escher_;
  std::unique_ptr<impl::ComputeShader> kernel_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PoseBufferLatchingShader);
};
}  // namespace hmd
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_HMD_POSE_BUFFER_LATCHING_SHADER_H_
