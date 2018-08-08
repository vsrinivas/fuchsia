// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/shadertoy/service/compiler.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include "garnet/examples/ui/shadertoy/service/renderer.h"
#include "lib/escher/impl/glsl_compiler.h"
#include "lib/escher/impl/mesh_shader_binding.h"

namespace {

constexpr char kVertexShaderSrc[] = R"GLSL(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inPosition;
// TODO: generate mesh without UV coords, and remove this.
layout(location = 2) in vec2 inUV;

out gl_PerVertex {
  vec4 gl_Position;
};

void main() {
  // Halfway between min and max depth.
  gl_Position = vec4(inPosition, 0, 1);
}
)GLSL";

constexpr char kFragmentShaderHeaderSrc[] = R"GLSL(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform sampler2D iChannel0;
layout(set = 0, binding = 1) uniform sampler2D iChannel1;
layout(set = 0, binding = 2) uniform sampler2D iChannel2;
layout(set = 0, binding = 3) uniform sampler2D iChannel3;

// These correspond to the C++ struct ShadertoyRenderer::Params.  In order to
// comply with the std430 layout used by Vulkan push constants, everything is
// represented here as 4-byte values, so that no additional padding is
// introduced.
layout(push_constant) uniform ShadertoyRendererParams {
  uniform float     iResolution[3];
  uniform float     iTime;
  uniform float     iTimeDelta;
  uniform int       iFrame;
  uniform float     iChannelTime[4];
  // vec3[4] ShadertoyRenderer::Params.iChannelResolution
  uniform float     iCR[12];
  uniform float     iMouse[4];
  uniform float     iDate[4];
  uniform float     iSampleRate;
} __pushed;

// Copy uniforms into the variables expected by Shadertoy programs.
vec3 iResolution = vec3(__pushed.iResolution[0],
                        __pushed.iResolution[1],
                        __pushed.iResolution[2]);
float iTime =      __pushed.iTime;
float iTimeDelta = __pushed.iTimeDelta;
int iFrame =       __pushed.iFrame;
float iChannelTime[4] = __pushed.iChannelTime;
vec3 iChannelResolution[4] =
         vec3[](vec3(__pushed.iCR[0], __pushed.iCR[1], __pushed.iCR[2]),
         vec3(__pushed.iCR[3], __pushed.iCR[4], __pushed.iCR[5]),
         vec3(__pushed.iCR[6], __pushed.iCR[7], __pushed.iCR[8]),
         vec3(__pushed.iCR[9], __pushed.iCR[10], __pushed.iCR[11]));
vec4 iMouse = vec4(__pushed.iMouse[0],
                   __pushed.iMouse[1],
                   __pushed.iMouse[2],
                   __pushed.iMouse[3]);
vec4 iDate = vec4(__pushed.iDate[0],
                  __pushed.iDate[1],
                  __pushed.iDate[2],
                  __pushed.iDate[3]);
float iSampleRate = __pushed.iSampleRate;

// Backward compatibility?  Some Shadertoy programs use this value, but it is
// not currently listed amongst those provided by the website.
float iGlobalTime = iTime;

layout(location = 0) out vec4 outColor;

void mainImage( out vec4 fragColor, in vec2 fragCoord);

void main() {
  vec4 color = vec4(0.0,0.0,0.0,1.0);
  vec2 swapped_y = {gl_FragCoord.x, iResolution.y-gl_FragCoord.y};
  mainImage(color, swapped_y);
  outColor = color;
}

// ******************* END of Compiler Fragment Shader header *********

)GLSL";

}  // anonymous namespace

namespace shadertoy {

Compiler::Compiler(async::Loop* loop, escher::EscherWeakPtr weak_escher,
                   vk::RenderPass render_pass,
                   vk::DescriptorSetLayout descriptor_set_layout)
    : loop_(loop),
      escher_(std::move(weak_escher)),
      model_data_(fxl::MakeRefCounted<escher::impl::ModelData>(escher_)),
      render_pass_(render_pass),
      descriptor_set_layout_(descriptor_set_layout) {
  FXL_DCHECK(render_pass_);
  FXL_DCHECK(descriptor_set_layout);
}

Compiler::~Compiler() {
  std::lock_guard<std::mutex> lock(mutex_);
  while (!requests_.empty()) {
    requests_.front().callback({PipelinePtr()});
    requests_.pop();
  }
  if (has_thread_) {
    // TODO: This isn't a big deal, because it only happens when the process
    // is shutting down, but it would be tidier to wait for the thread to
    // finish.
    FXL_LOG(WARNING) << "Destroying while compile thread is still active.";
  }
}

const vk::DescriptorSetLayoutCreateInfo&
Compiler::GetDescriptorSetLayoutCreateInfo() {
  constexpr uint32_t kNumBindings = 4;
  static vk::DescriptorSetLayoutBinding bindings[kNumBindings];
  static vk::DescriptorSetLayoutCreateInfo info;
  static vk::DescriptorSetLayoutCreateInfo* ptr = nullptr;
  if (!ptr) {
    auto& texture_binding_0 = bindings[0];
    auto& texture_binding_1 = bindings[1];
    auto& texture_binding_2 = bindings[2];
    auto& texture_binding_3 = bindings[3];

    texture_binding_0.binding = 0;
    texture_binding_0.descriptorType =
        vk::DescriptorType::eCombinedImageSampler;
    texture_binding_0.descriptorCount = 1;
    texture_binding_0.stageFlags = vk::ShaderStageFlagBits::eFragment;

    texture_binding_1.binding = 1;
    texture_binding_1.descriptorType =
        vk::DescriptorType::eCombinedImageSampler;
    texture_binding_1.descriptorCount = 1;
    texture_binding_1.stageFlags = vk::ShaderStageFlagBits::eFragment;

    texture_binding_2.binding = 2;
    texture_binding_2.descriptorType =
        vk::DescriptorType::eCombinedImageSampler;
    texture_binding_2.descriptorCount = 1;
    texture_binding_2.stageFlags = vk::ShaderStageFlagBits::eFragment;

    texture_binding_3.binding = 3;
    texture_binding_3.descriptorType =
        vk::DescriptorType::eCombinedImageSampler;
    texture_binding_3.descriptorCount = 1;
    texture_binding_3.stageFlags = vk::ShaderStageFlagBits::eFragment;

    info.bindingCount = kNumBindings;
    info.pBindings = bindings;
    ptr = &info;
  }
  return *ptr;
}

void Compiler::Compile(std::string glsl, ResultCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!has_thread_) {
    thread_ = std::thread([this] { this->ProcessRequestQueue(); });
    has_thread_ = true;
  }
  requests_.push({std::move(glsl), std::move(callback)});
}

void Compiler::ProcessRequestQueue() {
  while (true) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (requests_.empty()) {
      thread_.detach();
      has_thread_ = false;
      return;
    }
    Request req(std::move(requests_.front()));
    requests_.pop();
    lock.unlock();

    auto pipeline = CompileGlslToPipeline(req.glsl);

    async::PostTask(loop_->dispatcher(), [result = Result{std::move(pipeline)},
                                          callback = std::move(req.callback)] {
      callback(std::move(result));
    });
  }
}

PipelinePtr Compiler::CompileGlslToPipeline(const std::string& glsl_code) {
  auto vk_device = escher_->vulkan_context().device;

  std::future<escher::impl::SpirvData> vertex_spirv_future =
      glsl_compiler()->Compile(vk::ShaderStageFlagBits::eVertex,
                               {std::string(kVertexShaderSrc)}, std::string(),
                               "main");

  std::future<escher::impl::SpirvData> fragment_spirv_future =
      glsl_compiler()->Compile(
          vk::ShaderStageFlagBits::eFragment,
          {std::string(kFragmentShaderHeaderSrc) + glsl_code}, std::string(),
          "main");

  vk::ShaderModule vertex_module;
  {
    auto spirv = vertex_spirv_future.get();
    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = spirv.size() * sizeof(uint32_t);
    module_info.pCode = spirv.data();
    auto result = vk_device.createShaderModule(module_info);
    if (result.result != vk::Result::eSuccess) {
      FXL_LOG(WARNING) << "Failed to compile vertex shader.";
      return PipelinePtr();
    }
    vertex_module = result.value;
  }

  vk::ShaderModule fragment_module;
  {
    auto spirv = fragment_spirv_future.get();
    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = spirv.size() * sizeof(uint32_t);
    module_info.pCode = spirv.data();
    auto result = vk_device.createShaderModule(module_info);
    if (result.result != vk::Result::eSuccess) {
      FXL_LOG(WARNING) << "Failed to compile fragment shader.";
      vk_device.destroyShaderModule(vertex_module);
      return PipelinePtr();
    }
    fragment_module = result.value;
  }

  escher::MeshSpec mesh_spec;
  mesh_spec.flags =
      escher::MeshAttribute::kPosition2D | escher::MeshAttribute::kUV;

  auto pipeline = ConstructPipeline(vertex_module, fragment_module, mesh_spec);
  vk_device.destroyShaderModule(vertex_module);
  vk_device.destroyShaderModule(fragment_module);
  return pipeline;
}

PipelinePtr Compiler::ConstructPipeline(vk::ShaderModule vertex_module,
                                        vk::ShaderModule fragment_module,
                                        const escher::MeshSpec& mesh_spec) {
  vk::Device device = escher_->vulkan_context().device;

  // Depending on configuration, more dynamic states may be added later.
  vk::PipelineDynamicStateCreateInfo dynamic_state_info;
  std::vector<vk::DynamicState> dynamic_states{vk::DynamicState::eViewport,
                                               vk::DynamicState::eScissor};

  vk::PipelineShaderStageCreateInfo vertex_stage_info;
  vertex_stage_info.stage = vk::ShaderStageFlagBits::eVertex;
  vertex_stage_info.module = vertex_module;
  vertex_stage_info.pName = "main";

  vk::PipelineShaderStageCreateInfo fragment_stage_info;
  fragment_stage_info.stage = vk::ShaderStageFlagBits::eFragment;
  fragment_stage_info.module = fragment_module;
  fragment_stage_info.pName = "main";

  constexpr size_t kShaderStageCount = 2;
  vk::PipelineShaderStageCreateInfo shader_stages[kShaderStageCount] = {
      vertex_stage_info, fragment_stage_info};

  vk::PipelineVertexInputStateCreateInfo vertex_input_info;
  {
    auto& mesh_shader_binding = model_data_->GetMeshShaderBinding(mesh_spec);
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions =
        mesh_shader_binding.binding();
    vertex_input_info.vertexAttributeDescriptionCount =
        mesh_shader_binding.attributes().size();
    vertex_input_info.pVertexAttributeDescriptions =
        mesh_shader_binding.attributes().data();
  }

  vk::PipelineInputAssemblyStateCreateInfo input_assembly_info;
  input_assembly_info.topology = vk::PrimitiveTopology::eTriangleList;
  input_assembly_info.primitiveRestartEnable = false;

  vk::PipelineDepthStencilStateCreateInfo depth_stencil_info;
  depth_stencil_info.depthTestEnable = false;
  depth_stencil_info.depthWriteEnable = false;
  depth_stencil_info.depthBoundsTestEnable = false;
  depth_stencil_info.stencilTestEnable = true;

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

  vk::PushConstantRange push_constants;
  push_constants.stageFlags = vk::ShaderStageFlagBits::eFragment;
  push_constants.offset = 0;
  push_constants.size = sizeof(Renderer::Params);

  vk::PipelineLayoutCreateInfo pipeline_layout_info;
  pipeline_layout_info.setLayoutCount = 1;
  pipeline_layout_info.pSetLayouts = &descriptor_set_layout_;
  pipeline_layout_info.pushConstantRangeCount = 1;
  pipeline_layout_info.pPushConstantRanges = &push_constants;

  vk::PipelineLayout pipeline_layout;
  {
    auto result = device.createPipelineLayout(pipeline_layout_info, nullptr);
    FXL_DCHECK(result.result == vk::Result::eSuccess);
    pipeline_layout = result.value;
  }

  // All dynamic states have been accumulated, so finalize them.
  dynamic_state_info.dynamicStateCount =
      static_cast<uint32_t>(dynamic_states.size());
  dynamic_state_info.pDynamicStates = dynamic_states.data();

  vk::GraphicsPipelineCreateInfo pipeline_info;
  pipeline_info.stageCount = kShaderStageCount;
  pipeline_info.pStages = shader_stages;
  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly_info;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pDepthStencilState = &depth_stencil_info;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.pDynamicState = &dynamic_state_info;
  pipeline_info.layout = pipeline_layout;
  pipeline_info.renderPass = render_pass_;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = vk::Pipeline();

  vk::Pipeline pipeline;
  {
    auto result = device.createGraphicsPipeline(nullptr, pipeline_info);
    FXL_DCHECK(result.result == vk::Result::eSuccess);
    pipeline = result.value;
  }

  return fxl::MakeRefCounted<Pipeline>(device, pipeline, pipeline_layout);
}

escher::impl::GlslToSpirvCompiler* Compiler::glsl_compiler() {
  return escher_->glsl_compiler();
}

}  // namespace shadertoy
