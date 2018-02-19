// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/model_moment_shadow_map_pass.h"

#include "lib/escher/impl/model_data.h"

namespace {

constexpr char kFragmentShaderSourceCode[] = R"GLSL(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 outColor;

void main() {
  float z = gl_FragCoord.z;
  float z2 = z * z;
  float z3 = z * z2;
  float z4 = z * z3;
  outColor = vec4(z, z2, z3, z4);
}
)GLSL";

}  // namespace

namespace escher {
namespace impl {

ModelMomentShadowMapPass::ModelMomentShadowMapPass(
    ResourceRecycler* recycler,
    const ModelDataPtr& model_data,
    vk::Format color_format,
    vk::Format depth_format,
    uint32_t sample_count)
    : ModelShadowMapPass(recycler,
                         model_data,
                         color_format,
                         depth_format,
                         sample_count) {}

std::string ModelMomentShadowMapPass::GetFragmentShaderSourceCode(
    const ModelPipelineSpec& spec) {
  return kFragmentShaderSourceCode;
}

}  // namespace impl
}  // namespace escher
