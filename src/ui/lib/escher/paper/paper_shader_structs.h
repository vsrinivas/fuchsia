// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_SHADER_STRUCTS_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_SHADER_STRUCTS_H_

#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/renderer/frame.h"

// This file contains structs with the same names and fields as those found in
// the GLSL shader files.  In addition, they all declare the static fields
// kDescriptorSet and kDescriptorSet binding, which match the usage in the
// shader files.  These serve as documentation, and are also supported by
// the convenience function |NewPaperShaderUniformBinding()|, defined below.

namespace escher {

// Return a pair consisting of a typed pointer into per-frame uniform data, and
// a UniformBinding to that data.  StructT must declare the static fields
// kDescriptorSet and kDescriptorSetBinding; this requirement is fulfilled by
// all structs defined below.
template <typename StructT>
static std::pair<StructT*, UniformBinding> NewPaperShaderUniformBinding(const FramePtr& frame,
                                                                        size_t count = 1) {
  // TODO(fxbug.dev/7193): should be queried from device.
  constexpr vk::DeviceSize kMinUniformBufferOffsetAlignment = 256;
  UniformAllocation allocation =
      frame->AllocateUniform(count * sizeof(StructT), kMinUniformBufferOffsetAlignment);

  return std::make_pair(reinterpret_cast<StructT*>(allocation.host_ptr),
                        UniformBinding{.descriptor_set_index = StructT::kDescriptorSet,
                                       .binding_index = StructT::kDescriptorBinding,
                                       .buffer = allocation.buffer,
                                       .offset = allocation.offset,
                                       .size = allocation.size});

  static_assert(sizeof(StructT) % alignof(StructT) == 0, "must be packed.");
}

// Struct that defines a grepable common layout for C++ and GLSL code.
struct PaperShaderMeshInstance {
  mat4 model_transform;
  vec4 color;
  // TODO(fxbug.dev/7243): field for vertex-shader clip-planes.

  static constexpr size_t kDescriptorSet = 1;
  static constexpr size_t kDescriptorBinding = 0;
};

// Struct that defines a grepable common layout for C++ and GLSL code.
struct PaperShaderSceneData {
  vec3 ambient_light_color;

  static constexpr size_t kDescriptorSet = 0;
  static constexpr size_t kDescriptorBinding = 0;
};

// Struct that defines a grepable common layout for C++ and GLSL code.
struct PaperShaderLatchedPoseBuffer {
  static constexpr size_t kNumPoseFloats = 8;
  float padding[kNumPoseFloats];
  mat4 vp_matrix[2];

  static constexpr size_t kDescriptorSet = 0;
  static constexpr size_t kDescriptorBinding = 1;
};

static_assert(sizeof(hmd::Pose) == PaperShaderLatchedPoseBuffer::kNumPoseFloats * sizeof(float));

// Struct that defines a grepable common layout for C++ and GLSL code.
struct PaperShaderPointLight {
  vec4 position;
  vec4 color;
  float falloff;
  float __padding0;
  float __padding1;
  float __padding2;

  static constexpr size_t kDescriptorSet = 0;
  static constexpr size_t kDescriptorBinding = 2;
};

// Struct that defines common layout for C++ and GLSL code.
struct PaperShaderPushConstants {
  uint32_t light_index;
  uint32_t eye_index;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_SHADER_STRUCTS_H_
