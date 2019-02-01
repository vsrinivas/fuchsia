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

// The magic weights come from paper
// http://cg.cs.uni-bonn.de/aigaion2root/attachments/MomentShadowMapping.pdf
vec4 encode(vec4 x) {
  const mat4 kWeight = mat4(
    -2.07224649, 13.7948857237, 0.105877704, 9.7924062118,
    32.23703778, -59.4683975703, -1.9077466311, -33.7652110555,
    -68.571074599, 82.0359750338, 9.3496555107, 47.9456096605,
    39.3703274134, -35.364903257, -6.6543490743, -23.9728048165);
  const vec4 kBias = vec4(0.035955884801, 0., 0., 0.);
  return kWeight * x + kBias;
}

void main() {
  float z = gl_FragCoord.z;
  float z2 = z * z;
  float z3 = z * z2;
  float z4 = z * z3;
  outColor = encode(vec4(z, z2, z3, z4));
}
)GLSL";

}  // namespace

namespace escher {
namespace impl {

ModelMomentShadowMapPass::ModelMomentShadowMapPass(
    ResourceRecycler* recycler, const ModelDataPtr& model_data,
    vk::Format color_format, vk::Format depth_format, uint32_t sample_count)
    : ModelShadowMapPass(recycler, model_data, color_format, depth_format,
                         sample_count) {}

std::string ModelMomentShadowMapPass::GetFragmentShaderSourceCode(
    const ModelPipelineSpec& spec) {
  return kFragmentShaderSourceCode;
}

}  // namespace impl
}  // namespace escher
