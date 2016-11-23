// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/model_pipeline_cache.h"

#include "escher/geometry/types.h"
// TODO: move MeshSpecImpl into its own file, then remove this.
#include "escher/impl/mesh_impl.h"
#include "escher/impl/model_data.h"
#include "escher/impl/model_pipeline.h"
#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

namespace {

constexpr char g_vertex_src[] = R"GLSL(
  #version 450
  #extension GL_ARB_separate_shader_objects : enable

  // Attribute locations must match constants in mesh_impl.h
  layout(location = 0) in vec2 inPosition;
  layout(location = 2) in vec2 inUV;

  layout(location = 0) out vec2 fragUV;

  layout(set = 1, binding = 0) uniform PerObject {
    mat4 transform;
    vec4 color;
  };

  out gl_PerVertex {
    vec4 gl_Position;
  };

  void main() {
    // Halfway between min and max depth.
    gl_Position = transform * vec4(inPosition, 0, 1);
    // Divide by 25 to convert 'Material Stage' depth to range 0-1.
    gl_Position.z *= 0.04;
    fragUV = inUV;
  }
  )GLSL";

constexpr char g_vertex_wobble_src[] = R"GLSL(
    #version 450
    #extension GL_ARB_separate_shader_objects : enable

    // Attribute locations must match constants in mesh_impl.h
    layout(location = 0) in vec2 inPosition;
    layout(location = 1) in vec2 inPositionOffset;
    layout(location = 2) in vec2 inUV;
    layout(location = 3) in float inPerimeter;

    layout(location = 0) out vec2 fragUV;

    layout(set = 0, binding = 0) uniform PerModel {
      vec4 brightness;
      float time;
    };

    out gl_PerVertex {
      vec4 gl_Position;
    };

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
      mat4 transform;
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
      // ... if I try, the GLSL compiler produces SPIR-V,
      // but we fail when trying to create a vk::ShaderModule
      // from that SPIR-V.
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

    void main() {
      // TODO: workaround.  See discussion in PerObject struct, above.
      // float scale = EvalSineParams(sine_params_0) +
      //               EvalSineParams(sine_params_1) +
      //               EvalSineParams(sine_params_2);
      float scale = EvalSineParams_0() + EvalSineParams_1() + EvalSineParams_2();
      vec2 move = vec2(cos(time * 0.4f) * 200.f, sin(time) * 100.f);
      gl_Position = transform * vec4(inPosition + move + scale * inPositionOffset, 0, 1);

      // Divide by 25 to convert 'Material Stage' depth to range 0-1.
      gl_Position.z *= 0.04;
      fragUV = inUV;
    }
    )GLSL";

constexpr char g_fragment_src[] = R"GLSL(
  #version 450
  #extension GL_ARB_separate_shader_objects : enable

  layout(location = 0) in vec2 inUV;

  layout(set = 0, binding = 0) uniform PerModel {
    vec4 brightness;
    float time;
  };

  layout(set = 1, binding = 0) uniform PerObject {
    mat4 transform;
    vec4 color;
  };

  layout(set = 1, binding = 1) uniform sampler2D tex;

  layout(location = 0) out vec4 outColor;

  void main() {
    // TODO: would use mix(0.4f, 1.0f, brightness), but we currently don't have
    // access to the GLSL standard library.  See glsl_compiler.h.
    outColor = (0.4f + 0.6f * brightness) * color * texture(tex, inUV);
  }
  )GLSL";

constexpr char g_fragment_depth_prepass_src[] = R"GLSL(
  #version 450
  #extension GL_ARB_separate_shader_objects : enable

  layout(location = 0) in vec2 inUV;

  layout(location = 0) out vec4 outColor;

  void main() {
    outColor = vec4(1.f, 1.f, 1.f, 1.f);
  }
  )GLSL";

}  // namespace

ModelPipelineCache::ModelPipelineCache(vk::Device device,
                                       vk::RenderPass depth_prepass,
                                       vk::RenderPass lighting_pass)
    : device_(device),
      depth_prepass_(depth_prepass),
      lighting_pass_(lighting_pass) {}

ModelPipelineCacheOLD::ModelPipelineCacheOLD(vk::Device device,
                                             vk::RenderPass depth_prepass,
                                             vk::RenderPass lighting_pass,
                                             ModelData* model_data)
    : ModelPipelineCache(device, depth_prepass, lighting_pass),
      model_data_(model_data) {}

ModelPipelineCacheOLD::~ModelPipelineCacheOLD() {
  device_.waitIdle();
  pipelines_.clear();
}

ModelPipeline* ModelPipelineCacheOLD::GetPipeline(
    const ModelPipelineSpec& spec,
    const MeshSpecImpl& mesh_spec_impl) {
  // TODO: deal with hash collisions
  auto it = pipelines_.find(spec);
  if (it != pipelines_.end()) {
    return it->second.get();
  }
  auto new_pipeline = NewPipeline(spec, mesh_spec_impl);
  auto new_pipeline_ptr = new_pipeline.get();
  pipelines_[spec] = std::move(new_pipeline);
  return new_pipeline_ptr;
}

namespace {

// Creates a new PipelineLayout and Pipeline using only the provided arguments.
std::pair<vk::Pipeline, vk::PipelineLayout> NewPipelineHelper(
    vk::Device device,
    vk::ShaderModule vertex_module,
    vk::ShaderModule fragment_module,
    bool enable_depth_write,
    vk::CompareOp depth_compare_op,
    vk::RenderPass render_pass,
    std::vector<vk::DescriptorSetLayout> descriptor_set_layouts,
    const MeshSpecImpl& mesh_spec_impl) {
  vk::PipelineShaderStageCreateInfo vertex_stage_info;
  vertex_stage_info.stage = vk::ShaderStageFlagBits::eVertex;
  vertex_stage_info.module = vertex_module;
  vertex_stage_info.pName = "main";

  vk::PipelineShaderStageCreateInfo fragment_stage_info;
  fragment_stage_info.stage = vk::ShaderStageFlagBits::eFragment;
  fragment_stage_info.module = fragment_module;
  fragment_stage_info.pName = "main";

  vk::PipelineShaderStageCreateInfo shader_stages[] = {vertex_stage_info,
                                                       fragment_stage_info};

  vk::PipelineVertexInputStateCreateInfo vertex_input_info;
  vertex_input_info.vertexBindingDescriptionCount = 1;
  vertex_input_info.pVertexBindingDescriptions = &mesh_spec_impl.binding;
  vertex_input_info.vertexAttributeDescriptionCount =
      mesh_spec_impl.attributes.size();
  vertex_input_info.pVertexAttributeDescriptions =
      mesh_spec_impl.attributes.data();

  vk::PipelineInputAssemblyStateCreateInfo input_assembly_info;
  input_assembly_info.topology = vk::PrimitiveTopology::eTriangleList;
  input_assembly_info.primitiveRestartEnable = false;

  vk::PipelineDepthStencilStateCreateInfo depth_stencil_info;
  depth_stencil_info.depthTestEnable = true;
  depth_stencil_info.depthWriteEnable = enable_depth_write;
  depth_stencil_info.depthCompareOp = depth_compare_op;
  depth_stencil_info.depthBoundsTestEnable = false;
  depth_stencil_info.stencilTestEnable = false;

  // This is set dynamically during rendering.
  vk::Viewport viewport;
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = 0.f;
  viewport.height = 0.f;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 0.0f;

  // This is set dynamically during rendering.
  vk::Rect2D scissor;
  scissor.offset = vk::Offset2D{0, 0};
  scissor.extent = vk::Extent2D{0, 0};

  vk::PipelineViewportStateCreateInfo viewport_state;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = &viewport;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = &scissor;

  vk::PipelineRasterizationStateCreateInfo rasterizer;
  rasterizer.depthClampEnable = false;
  rasterizer.rasterizerDiscardEnable = false;
  rasterizer.polygonMode = vk::PolygonMode::eFill;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = vk::CullModeFlagBits::eBack;
  rasterizer.frontFace = vk::FrontFace::eClockwise;
  rasterizer.depthBiasEnable = false;

  vk::PipelineMultisampleStateCreateInfo multisampling;
  multisampling.sampleShadingEnable = false;
  multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

  vk::PipelineColorBlendAttachmentState color_blend_attachment;
  color_blend_attachment.colorWriteMask =
      vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
      vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
  color_blend_attachment.blendEnable = false;

  vk::PipelineColorBlendStateCreateInfo color_blending;
  color_blending.logicOpEnable = false;
  color_blending.logicOp = vk::LogicOp::eCopy;
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &color_blend_attachment;
  color_blending.blendConstants[0] = 0.0f;
  color_blending.blendConstants[1] = 0.0f;
  color_blending.blendConstants[2] = 0.0f;
  color_blending.blendConstants[3] = 0.0f;

  vk::PipelineDynamicStateCreateInfo dynamic_state;
  const uint32_t kDynamicStateCount = 2;
  vk::DynamicState dynamic_states[] = {vk::DynamicState::eViewport,
                                       vk::DynamicState::eScissor};
  dynamic_state.dynamicStateCount = kDynamicStateCount;
  dynamic_state.pDynamicStates = dynamic_states;

  vk::PipelineLayoutCreateInfo pipeline_layout_info;
  pipeline_layout_info.setLayoutCount =
      static_cast<uint32_t>(descriptor_set_layouts.size());
  pipeline_layout_info.pSetLayouts = descriptor_set_layouts.data();
  pipeline_layout_info.pushConstantRangeCount = 0;

  vk::PipelineLayout pipeline_layout = ESCHER_CHECKED_VK_RESULT(
      device.createPipelineLayout(pipeline_layout_info, nullptr));

  vk::GraphicsPipelineCreateInfo pipeline_info;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = shader_stages;
  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly_info;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pDepthStencilState = &depth_stencil_info;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.pDynamicState = &dynamic_state;
  pipeline_info.layout = pipeline_layout;
  pipeline_info.renderPass = render_pass;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

  vk::Pipeline pipeline = ESCHER_CHECKED_VK_RESULT(
      device.createGraphicsPipeline(nullptr, pipeline_info));

  return {pipeline, pipeline_layout};
}
}  // namespace

std::unique_ptr<ModelPipeline> ModelPipelineCacheOLD::NewPipeline(
    const ModelPipelineSpec& spec,
    const MeshSpecImpl& mesh_spec_impl) {
  // TODO: create customized pipelines for different shapes/materials/etc.

  std::future<SpirvData> vertex_spirv_future;
  std::future<SpirvData> fragment_spirv_future;

  // The wobble modifier causes a different vertex shader to be used.
  if (spec.shape_modifiers & ShapeModifier::kWobble) {
    vertex_spirv_future =
        compiler_.Compile(vk::ShaderStageFlagBits::eVertex,
                          {{g_vertex_wobble_src}}, std::string(), "main");
  } else {
    vertex_spirv_future =
        compiler_.Compile(vk::ShaderStageFlagBits::eVertex, {{g_vertex_src}},
                          std::string(), "main");
  }

  // The depth-only pre-pass uses a different renderpass, cheap fragment shader,
  // and different depth test settings.
  vk::RenderPass render_pass = depth_prepass_;
  bool enable_depth_write = true;
  vk::CompareOp depth_compare_op = vk::CompareOp::eLess;
  if (spec.use_depth_prepass) {
    // Use cheap fragment shader, since the results will be discarded.
    fragment_spirv_future = compiler_.Compile(
        vk::ShaderStageFlagBits::eFragment, {{g_fragment_depth_prepass_src}},
        std::string(), "main");
  } else {
    render_pass = lighting_pass_;
    // Don't write to the depth buffer; we re-use the buffer generated in the
    // depth pre-pass.  Must modify the comparison operator, otherwise nothing
    // would appear, because the nearest objects would have the exact same depth
    // as in the depth buffer.
    enable_depth_write = false;
    depth_compare_op = vk::CompareOp::eLessOrEqual;
    fragment_spirv_future =
        compiler_.Compile(vk::ShaderStageFlagBits::eFragment,
                          {{g_fragment_src}}, std::string(), "main");
  }

  // Wait for completion of asynchronous shader compilation.
  vk::ShaderModule vertex_module;
  {
    SpirvData spirv = vertex_spirv_future.get();

    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = spirv.size() * sizeof(uint32_t);
    module_info.pCode = spirv.data();
    vertex_module =
        ESCHER_CHECKED_VK_RESULT(device_.createShaderModule(module_info));
  }
  vk::ShaderModule fragment_module;
  {
    SpirvData spirv = fragment_spirv_future.get();

    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = spirv.size() * sizeof(uint32_t);
    module_info.pCode = spirv.data();
    fragment_module =
        ESCHER_CHECKED_VK_RESULT(device_.createShaderModule(module_info));
  }

  auto pipeline_and_layout = NewPipelineHelper(
      device_, vertex_module, fragment_module, enable_depth_write,
      depth_compare_op, render_pass,
      {model_data_->per_model_layout(), model_data_->per_object_layout()},
      mesh_spec_impl);

  device_.destroyShaderModule(vertex_module);
  device_.destroyShaderModule(fragment_module);

  return std::make_unique<ModelPipeline>(
      spec, device_, pipeline_and_layout.first, pipeline_and_layout.second);
}

}  // namespace impl
}  // namespace escher
