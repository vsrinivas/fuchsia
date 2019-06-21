// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_graphics_pipeline.h"

#include <unistd.h>

#include <vector>

#include "utils.h"
#include "vulkan_fixed_functions.h"
#include "vulkan_shader.h"

VulkanGraphicsPipeline::VulkanGraphicsPipeline(
    std::shared_ptr<VulkanLogicalDevice> device, const VkExtent2D &extent,
    const VkRenderPass &render_pass)
    : initialized_(false), device_(device), extent_(extent) {
  params_ = std::make_unique<InitParams>(render_pass);
}

VulkanGraphicsPipeline::~VulkanGraphicsPipeline() {
  if (initialized_) {
    vkDestroyPipelineLayout(device_->device(), pipeline_layout_, nullptr);
    vkDestroyPipeline(device_->device(), graphics_pipeline_, nullptr);
  }
}

VulkanGraphicsPipeline::InitParams::InitParams(const VkRenderPass &render_pass)
    : render_pass_(render_pass) {}

bool VulkanGraphicsPipeline::Init() {
  if (initialized_) {
    RTN_MSG(false, "VulkanGraphicsPipeline is already initialized.\n");
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
  snprintf(vert_shader, PATH_MAX,
           "%s/host_x64/obj/garnet/lib/vulkan/tests/vkprimer/vert.spv", cwd);
  char frag_shader[PATH_MAX];
  snprintf(frag_shader, PATH_MAX,
           "%s/host_x64/obj/garnet/lib/vulkan/tests/vkprimer/frag.spv", cwd);
#endif

  if (!VulkanShader::ReadFile(vert_shader, &vert_shader_buffer)) {
    RTN_MSG(false, "Can't read vertex spv file.\n");
  }
  if (!VulkanShader::ReadFile(frag_shader, &frag_shader_buffer)) {
    RTN_MSG(false, "Can't read fragment spv file.\n");
  }

  const VkDevice &device = device_->device();

  VkShaderModule vert_shader_module;
  if (!VulkanShader::CreateShaderModule(device, vert_shader_buffer,
                                        &vert_shader_module)) {
    RTN_MSG(false, "Can't create vertex shader module.\n");
  }

  VkShaderModule frag_shader_module;
  if (!VulkanShader::CreateShaderModule(device, frag_shader_buffer,
                                        &frag_shader_module)) {
    RTN_MSG(false, "Can't create fragment shader module.\n");
  }

  VkPipelineShaderStageCreateInfo vert_stage_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vert_shader_module,
      .pName = "main",
  };

  VkPipelineShaderStageCreateInfo frag_stage_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = frag_shader_module,
      .pName = "main",
  };

  VkPipelineShaderStageCreateInfo shader_stages[] = {
      vert_stage_create_info,
      frag_stage_create_info,
  };

  VulkanFixedFunctions fixed_functions(extent_);

  VkPipelineLayoutCreateInfo pipeline_layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pushConstantRangeCount = 0,
      .setLayoutCount = 0,
  };

  auto err = vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr,
                                    &pipeline_layout_);
  if (VK_SUCCESS != err) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create pipeline layout.\n", err);
  }

  VkGraphicsPipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .basePipelineIndex = -1,
      .basePipelineHandle = VK_NULL_HANDLE,
      .pColorBlendState = &fixed_functions.color_blending_info(),
      .pDepthStencilState = nullptr,  // optional
      .pDynamicState = nullptr,       // optional
      .pInputAssemblyState = &fixed_functions.input_assembly_info(),
      .layout = pipeline_layout_,
      .pMultisampleState = &fixed_functions.multisample_info(),
      .pRasterizationState = &fixed_functions.rasterizer_info(),
      .renderPass = params_->render_pass_,
      .stageCount = 2,
      .pStages = shader_stages,
      .subpass = 0,
      .pVertexInputState = &fixed_functions.vertex_input_info(),
      .pViewportState = &fixed_functions.viewport_info(),
  };

  err = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                  nullptr, &graphics_pipeline_);
  if (VK_SUCCESS != err) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create graphics pipeline.\n",
            err);
  }

  vkDestroyShaderModule(device, frag_shader_module, nullptr);
  vkDestroyShaderModule(device, vert_shader_module, nullptr);

  params_.reset();
  initialized_ = true;

  return true;
}
