// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkprimer/common/pipeline.h"

#include <unistd.h>

#include <vector>

#include "src/graphics/examples/vkprimer/common/fixed_functions.h"
#include "src/graphics/examples/vkprimer/common/shader.h"
#include "src/graphics/examples/vkprimer/common/utils.h"

namespace vkp {

Pipeline::Pipeline(std::shared_ptr<vk::Device> device, const vk::Extent2D &extent,
                   std::shared_ptr<RenderPass> vkp_render_pass)
    : initialized_(false),
      device_(device),
      extent_(extent),
      vkp_render_pass_(std::move(vkp_render_pass)) {}

Pipeline::~Pipeline() {
  if (initialized_) {
    device_->destroyPipelineLayout(pipeline_layout_);
    device_->destroyPipeline(pipeline_);
    initialized_ = false;
  }
}

bool Pipeline::Init() {
  if (initialized_) {
    RTN_MSG(false, "Pipeline is already initialized.\n");
  }

  std::vector<char> vert_shader_buffer;
  std::vector<char> frag_shader_buffer;

#ifdef __Fuchsia__
  const char *vert_shader = "/pkg/data/shaders/vert.spv";
  const char *frag_shader = "/pkg/data/shaders/frag.spv";
#else
  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    RTN_MSG(false, "Can't get current working directory.\n");
  }
  char vert_shader[PATH_MAX];
  snprintf(vert_shader, PATH_MAX, "%s/host_x64/obj/src/graphics/examples/vkprimer/vert.spv", cwd);
  char frag_shader[PATH_MAX];
  snprintf(frag_shader, PATH_MAX, "%s/host_x64/obj/src/graphics/examples/vkprimer/frag.spv", cwd);
#endif

  if (!Shader::ReadFile(vert_shader, &vert_shader_buffer)) {
    RTN_MSG(false, "Can't read vertex spv file.\n");
  }
  if (!Shader::ReadFile(frag_shader, &frag_shader_buffer)) {
    RTN_MSG(false, "Can't read fragment spv file.\n");
  }

  auto [r_vshader_module, vshader_module] =
      Shader::CreateShaderModule(*device_, vert_shader_buffer);
  RTN_IF_VKH_ERR(false, r_vshader_module, "Failed to create vtx shader module.\n");
  vk::UniqueShaderModule vert_shader_module = std::move(vshader_module);

  auto [r_fshader_module, fshader_module] =
      Shader::CreateShaderModule(*device_, frag_shader_buffer);
  RTN_IF_VKH_ERR(false, r_fshader_module, "Failed to create frag shader module.\n");
  vk::UniqueShaderModule frag_shader_module = std::move(fshader_module);

  vk::PipelineShaderStageCreateInfo shader_stages[2];
  shader_stages[0].module = *vert_shader_module;
  shader_stages[0].pName = "main";
  shader_stages[1].module = *frag_shader_module;
  shader_stages[1].stage = vk::ShaderStageFlagBits::eFragment;
  shader_stages[1].pName = "main";

  FixedFunctions fixed_functions(extent_);

  vk::PipelineLayoutCreateInfo pipeline_layout_info;
  auto rv_layout = device_->createPipelineLayout(pipeline_layout_info);
  if (vk::Result::eSuccess != rv_layout.result) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create pipeline layout.\n", rv_layout.result);
  }
  pipeline_layout_ = rv_layout.value;

  FixedFunctions &ff = fixed_functions;
  vk::GraphicsPipelineCreateInfo pipeline_info;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = shader_stages;
  pipeline_info.setPVertexInputState(&ff.vertex_input_info());
  pipeline_info.setPInputAssemblyState(&ff.input_assembly_info());
  pipeline_info.setPViewportState(&ff.viewport_info());
  pipeline_info.setPRasterizationState(&ff.rasterizer_info());
  pipeline_info.setPMultisampleState(&ff.multisample_info());
  pipeline_info.setPColorBlendState(&ff.color_blending_info());
  pipeline_info.layout = pipeline_layout_;
  pipeline_info.renderPass = vkp_render_pass_->get();
  pipeline_info.basePipelineIndex = -1;
  // TODO(fxbug.dev/62319): Use vk::Device::createGraphicsPipelinesUnique once
  // the invalid copy-ctor usage is fixed.
  auto [rv_pipelines, pipelines] =
      device_->createGraphicsPipelines(vk::PipelineCache(), {pipeline_info});
  if (vk::Result::eSuccess != rv_pipelines) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create pipelines.\n", rv_pipelines);
  }
  pipeline_ = pipelines[0];

  initialized_ = true;

  return true;
}

const vk::Pipeline &Pipeline::get() const { return pipeline_; }

}  // namespace vkp
