// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_PUBLIC_LIB_ESCHER_HMD_POSE_BUFFER_LATCHING_SHADER_H_
#define GARNET_PUBLIC_LIB_ESCHER_HMD_POSE_BUFFER_LATCHING_SHADER_H_

#include "lib/escher/impl/compute_shader.h"

namespace escher {
namespace hmd {

// Uses a simple compute shader to latch a pose out of the pose buffer.
// See //garnet/public/lib/ui/scenic/fidl/ops.fidl for details on pose buffer.
class PoseBufferLatchingShader {
 public:
  PoseBufferLatchingShader(Escher* escher,
                           BufferPtr& pose_buffer,
                           uint32_t num_entries,
                           uint64_t base_time,
                           uint64_t time_interval);

  // Latches a pose from the pose buffer for |latch_time|
  // The returned buffer will contain the raw latched pose as well as a
  // ViewProjection matrix computed from |pose_buffer| and |camera| as
  // camera->transform() * mat4(latched_pose) * camera->projection()
  // These output values will be layed out in the output buffer as follows:
  //
  // struct OutputBuffer {
  //   mat4  vp_matrix;
  //   struct Pose latched_pose;
  // }
  //
  // For details on pose buffers and the layout of the Pose struct see
  // //garnet/public/lib/ui/scenic/fidl/ops.fidl
  BufferPtr LatchPose(const FramePtr& frame,
                      const Camera& camera,
                      uint64_t latch_time,
                      bool host_accessible_output = false);

 private:
  Escher* const escher_;
  std::unique_ptr<impl::ComputeShader> kernel_;
  BufferPtr pose_buffer_;
  uint32_t num_entries_;
  uint64_t base_time_;
  uint64_t time_interval_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PoseBufferLatchingShader);
};
}
}

#endif  // GARNET_PUBLIC_LIB_ESCHER_HMD_POSE_BUFFER_LATCHING_SHADER_H_