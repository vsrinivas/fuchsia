// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/model_render_pass.h"

#include "lib/escher/impl/model_data.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/resources/resource_recycler.h"

namespace escher {
namespace impl {

static constexpr uint32_t kColorAttachmentCount = 1;
static constexpr uint32_t kDepthAttachmentCount = 1;
static constexpr uint32_t kAttachmentReferenceCount = 2;
static constexpr uint32_t kSubpassCount = 1;
static constexpr uint32_t kSubpassDependencyCount = 2;
static constexpr uint32_t kSoleSubpassIndex = 0;

ModelRenderPass::ModelRenderPass(ResourceRecycler* recycler,
                                 vk::Format color_format,
                                 vk::Format depth_format, uint32_t sample_count)
    : RenderPass(recycler, kColorAttachmentCount, kDepthAttachmentCount,
                 kAttachmentReferenceCount, kSubpassCount,
                 kSubpassDependencyCount),
      sample_count_(sample_count) {
  // Sanity check that these indices correspond to the first color and depth
  // attachments, respectively.
  FXL_DCHECK(kColorAttachmentIndex == color_attachment_index(0));
  FXL_DCHECK(kDepthAttachmentIndex == depth_attachment_index(0));

  vk::AttachmentDescription* color_attachment =
      attachment(kColorAttachmentIndex);
  vk::AttachmentDescription* depth_attachment =
      attachment(kDepthAttachmentIndex);
  vk::AttachmentReference* color_reference =
      attachment_reference(kColorAttachmentIndex);
  vk::AttachmentReference* depth_reference =
      attachment_reference(kDepthAttachmentIndex);
  vk::SubpassDescription* single_subpass =
      subpass_description(kSoleSubpassIndex);
  vk::SubpassDependency* input_dependency = subpass_dependency(0);
  vk::SubpassDependency* output_dependency = subpass_dependency(1);

  // Common to all subclasses.
  color_attachment->format = color_format;
  color_attachment->samples = SampleCountFlagBitsFromInt(sample_count);
  depth_attachment->format = depth_format;
  depth_attachment->samples = SampleCountFlagBitsFromInt(sample_count);
  depth_attachment->stencilLoadOp = vk::AttachmentLoadOp::eClear;
  depth_attachment->stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

  color_reference->attachment = kColorAttachmentIndex;
  color_reference->layout = vk::ImageLayout::eColorAttachmentOptimal;
  depth_reference->attachment = kDepthAttachmentIndex;
  depth_reference->layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

  single_subpass->pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  single_subpass->colorAttachmentCount = 1;
  single_subpass->pColorAttachments = color_reference;
  single_subpass->pDepthStencilAttachment = depth_reference;
  // No other subpasses to sample from.
  single_subpass->inputAttachmentCount = 0;

  // The first dependency transitions from the final layout from the previous
  // render pass, to the initial layout of this one.
  input_dependency->srcSubpass = VK_SUBPASS_EXTERNAL;  // not in vulkan.hpp
  input_dependency->dstSubpass = kSoleSubpassIndex;
  input_dependency->srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
  input_dependency->dstStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;
  input_dependency->srcAccessMask = vk::AccessFlagBits::eMemoryRead;
  input_dependency->dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                    vk::AccessFlagBits::eColorAttachmentWrite;
  input_dependency->dependencyFlags = vk::DependencyFlagBits::eByRegion;

  // The second dependency describes the transition from the initial to final
  // layout.
  output_dependency->srcSubpass = kSoleSubpassIndex;
  output_dependency->dstSubpass = VK_SUBPASS_EXTERNAL;
  output_dependency->srcStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;
  output_dependency->dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
  output_dependency->srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                     vk::AccessFlagBits::eColorAttachmentWrite;
  output_dependency->dstAccessMask = vk::AccessFlagBits::eMemoryRead;
  output_dependency->dependencyFlags = vk::DependencyFlagBits::eByRegion;
}

void ModelRenderPass::CreateRenderPassAndPipelineCache(
    ModelDataPtr model_data) {
  CreateRenderPass();
  // TODO: ModelPipelineCache doesn't need to be a resource if this render pass
  // is one.
  pipeline_cache_ = fxl::MakeRefCounted<ModelPipelineCache>(
      static_cast<ResourceRecycler*>(owner()), std::move(model_data), this);
}

static constexpr char kVertexShaderPreamble[] = R"GLSL(
#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
  vec4 gl_Position;
};

layout(set = 0, binding = 0) uniform PerModel {
  vec2 frag_coord_to_uv_multiplier;
  float time;
  vec3 ambient_light_intensity;
  vec3 direct_light_intensity;
};

// Use binding 2 to avoid potential collision with PerModelSampler
layout(set = 0, binding = 2) uniform ViewProjection {
  mat4 vp_matrix;
};

// Attribute locations must match constants in model_data.h
)GLSL";

static constexpr char kVertexShaderPosition[] = R"GLSL(
layout(set = 1, binding = 0) uniform PerObject {
  mat4 model_transform;
  mat4 light_transform;
  vec4 color;
};

vec4 ComputeVertexPosition() {
  return vec4(inPosition, 1);
}
)GLSL";

static constexpr char kVertexShaderWobblePosition[] = R"GLSL(
// TODO: unused.  See discussion in PerObject struct, below.
struct SineParams {
  float speed;
  float amplitude;
  float frequency;
};
const int kNumSineParams = 3;
float EvalSineParams(SineParams params) {
  float arg = params.frequency * inPerimeter + params.speed * time;
  return params.amplitude * sin(arg);
}

layout(set = 1, binding = 0) uniform PerObject {
  mat4 model_transform;
  mat4 light_transform;
  vec4 color;
  // Corresponds to ModifierWobble::SineParams[0].
  float speed_0;
  float amplitude_0;
  float frequency_0;
  // Corresponds to ModifierWobble::SineParams[1].
  float speed_1;
  float amplitude_1;
  float frequency_1;
  // Corresponds to ModifierWobble::SineParams[2].
  float speed_2;
  float amplitude_2;
  float frequency_2;
  // TODO: for some reason, I can't say:
  //   SineParams sine_params[kNumSineParams];
  // nor:
  //   SineParams sine_params_0;
  //   SineParams sine_params_1;
  //   SineParams sine_params_2;
  // ... if I try, the GLSL compiler produces SPIR-V, but the "SC"
  // validation layer complains when trying to create a vk::ShaderModule
  // from that SPIR-V.  Note: if we ignore the warning and proceed, nothing
  // explodes.  Nevertheless, we'll leave it this way for now, to be safe.
};

// TODO: workaround.  See discussion in PerObject struct, above.
float EvalSineParams_0() {
  float arg = frequency_0 * inPerimeter + speed_0 * time;
  return amplitude_0 * sin(arg);
}
float EvalSineParams_1() {
  float arg = frequency_1 * inPerimeter + speed_1 * time;
  return amplitude_1 * sin(arg);
}
float EvalSineParams_2() {
  float arg = frequency_2 * inPerimeter + speed_2 * time;
  return amplitude_2 * sin(arg);
}

vec4 ComputeVertexPosition() {
  // TODO: workaround.  See discussion in PerObject struct, above.
  // float scale = EvalSineParams(sine_params_0) +
  //               EvalSineParams(sine_params_1) +
  //               EvalSineParams(sine_params_2);
  float offset_scale = EvalSineParams_0() + EvalSineParams_1() + EvalSineParams_2();
  return vec4(inPosition + offset_scale * inPositionOffset, 1);
}
)GLSL";

std::string ModelRenderPass::GetVertexShaderSourceCode(
    const ModelPipelineSpec& spec) {
  std::ostringstream src;
  src << kVertexShaderPreamble;
  if (spec.mesh_spec.flags & MeshAttribute::kPosition2D ||
      spec.mesh_spec.flags & MeshAttribute::kPosition3D) {
    // NOTE: this shader works with both 2D and 3D meshes.  In the former case,
    // Vulkan fills the Z-coordinate of |inPosition| with the default value: 0.
    // See section 20.2 of the Vulkan spec (as of Vulkan 1.0.57).
    src << "layout(location = 0) in vec3 inPosition;\n";
  }

  if (spec.mesh_spec.flags & MeshAttribute::kPositionOffset) {
    src << "layout(location = 1) in vec3 inPositionOffset;\n";
  }
  if (spec.mesh_spec.flags & MeshAttribute::kUV) {
    src << "layout(location = 2) in vec2 inUV;\n";
  }
  if (spec.mesh_spec.flags & MeshAttribute::kPerimeterPos) {
    src << "layout(location = 3) in float inPerimeter;\n";
  }

  if (spec.shape_modifiers & ShapeModifier::kWobble) {
    src << kVertexShaderWobblePosition;
  } else {
    src << kVertexShaderPosition;
  }

  src << GetVertexShaderMainSourceCode() << std::endl;
  return src.str();
}

}  // namespace impl
}  // namespace escher
