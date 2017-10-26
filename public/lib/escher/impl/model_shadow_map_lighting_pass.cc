// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/model_shadow_map_lighting_pass.h"

#include "lib/escher/impl/model_data.h"

namespace escher {
namespace impl {

static const char kVertexShaderMainSourceCode[] = R"GLSL(
layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 shadowPos;

void main() {
  vec4 pos = ComputeVertexPosition();
  gl_Position = camera_transform * pos;
  shadowPos = light_transform * pos;
  fragUV = inUV;
}
)GLSL";

static const char kFragmentShaderSourceCode[] = R"GLSL(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 shadowPos;

layout(set = 0, binding = 0) uniform PerModel {
  vec2 frag_coord_to_uv_multiplier;
  float time;
	vec3 ambient_light_intensity;
	vec3 direct_light_intensity;
};

layout(set = 0, binding = 1) uniform sampler2D shadow_map_tex;

layout(set = 1, binding = 0) uniform PerObject {
  mat4 camera_transform;
  mat4 light_transform;
  vec4 color;
};

layout(set = 1, binding = 1) uniform sampler2D material_tex;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 light = ambient_light_intensity;

  vec4 shadowUV = (shadowPos / shadowPos.w);
  // TODO: do better than 0.0004 fudge factor.
  if (texture(shadow_map_tex, shadowUV.xy).r >= (shadowUV.z - 0.0004)) {
    light += direct_light_intensity;
  }

  outColor = vec4(light, 1.f) * color * texture(material_tex, inUV);
}
)GLSL";

ModelShadowMapLightingPass::ModelShadowMapLightingPass(
    ResourceRecycler* recycler,
    ModelDataPtr model_data,
    vk::Format color_format,
    vk::Format depth_format,
    uint32_t sample_count)
    : ModelRenderPass(recycler, color_format, depth_format, sample_count) {
  vk::AttachmentDescription* color_attachment =
      attachment(kColorAttachmentIndex);
  vk::AttachmentDescription* depth_attachment =
      attachment(kDepthAttachmentIndex);

  color_attachment->loadOp = vk::AttachmentLoadOp::eClear;
  // TODO: necessary to store if we resolve as part of the render-pass?
  color_attachment->storeOp = vk::AttachmentStoreOp::eStore;
  color_attachment->initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
  color_attachment->finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
  depth_attachment->loadOp = vk::AttachmentLoadOp::eClear;
  depth_attachment->storeOp = vk::AttachmentStoreOp::eDontCare;
  depth_attachment->initialLayout = vk::ImageLayout::eUndefined;
  depth_attachment->finalLayout =
      vk::ImageLayout::eDepthStencilAttachmentOptimal;

  // We have finished specifying the render-pass.  Now create it.
  CreateRenderPassAndPipelineCache(std::move(model_data));
}

std::string ModelShadowMapLightingPass::GetFragmentShaderSourceCode(
    const ModelPipelineSpec& spec) {
  return kFragmentShaderSourceCode;
}

std::string ModelShadowMapLightingPass::GetVertexShaderMainSourceCode() {
  return kVertexShaderMainSourceCode;
}

}  // namespace impl
}  // namespace escher
