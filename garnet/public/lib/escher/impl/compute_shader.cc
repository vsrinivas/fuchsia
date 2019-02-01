// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/compute_shader.h"

#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/impl/glsl_compiler.h"
#include "lib/escher/impl/vk/pipeline.h"
#include "lib/escher/impl/vk/pipeline_spec.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/vk/texture.h"
#include "lib/escher/vk/vulkan_context.h"

namespace escher {
namespace impl {

namespace {

// Used by ComputeShader constructor.
inline std::vector<vk::DescriptorSetLayoutBinding> CreateLayoutBindings(
    const std::vector<vk::ImageLayout>& layouts,
    const std::vector<vk::DescriptorType>& buffer_types) {
  std::vector<vk::DescriptorSetLayoutBinding> result;
  for (uint32_t index = 0; index < layouts.size(); ++index) {
    vk::DescriptorType descriptor_type;
    switch (layouts[index]) {
      case vk::ImageLayout::eShaderReadOnlyOptimal:
        descriptor_type = vk::DescriptorType::eCombinedImageSampler;
        break;
      case vk::ImageLayout::eGeneral:
        descriptor_type = vk::DescriptorType::eStorageImage;
        break;
      default:
        FXL_LOG(ERROR) << "unsupported layout: "
                       << vk::to_string(layouts[index]);
        FXL_CHECK(false);
        descriptor_type = vk::DescriptorType::eStorageImage;
    }
    result.push_back({index, descriptor_type, 1,
                      vk::ShaderStageFlagBits::eCompute, nullptr});
  }
  for (uint32_t i = 0; i < buffer_types.size(); ++i) {
    uint32_t binding = i + static_cast<uint32_t>(layouts.size());
    result.push_back({binding, buffer_types[i], 1,
                      vk::ShaderStageFlagBits::eCompute, nullptr});
  }
  return result;
}

// Used by ComputeShader constructor.
inline vk::DescriptorSetLayoutCreateInfo CreateDescriptorSetLayoutCreateInfo(
    const std::vector<vk::DescriptorSetLayoutBinding>& bindings) {
  vk::DescriptorSetLayoutCreateInfo info;
  info.bindingCount = static_cast<uint32_t>(bindings.size());
  info.pBindings = bindings.data();
  return info;
}

// Used by ComputeShader constructor.
PipelinePtr CreatePipeline(vk::Device device,
                           vk::DescriptorSetLayout descriptor_set_layout,
                           uint32_t push_constants_size,
                           const char* source_code,
                           GlslToSpirvCompiler* compiler) {
  vk::ShaderModule module;
  {
    SpirvData spirv = compiler
                          ->Compile(vk::ShaderStageFlagBits::eCompute,
                                    {{source_code}}, std::string(), "main")
                          .get();

    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = spirv.size() * sizeof(uint32_t);
    module_info.pCode = spirv.data();
    module = ESCHER_CHECKED_VK_RESULT(device.createShaderModule(module_info));
  }

  vk::PushConstantRange push_constants;
  push_constants.stageFlags = vk::ShaderStageFlagBits::eCompute;
  push_constants.offset = 0;
  push_constants.size = push_constants_size;

  vk::PipelineLayoutCreateInfo pipeline_layout_info;
  pipeline_layout_info.setLayoutCount = 1;
  pipeline_layout_info.pSetLayouts = &descriptor_set_layout;
  pipeline_layout_info.pushConstantRangeCount = push_constants_size > 0 ? 1 : 0;
  pipeline_layout_info.pPushConstantRanges =
      push_constants_size > 0 ? &push_constants : nullptr;

  auto pipeline_layout = fxl::MakeRefCounted<PipelineLayout>(
      device, ESCHER_CHECKED_VK_RESULT(
                  device.createPipelineLayout(pipeline_layout_info, nullptr)));

  vk::PipelineShaderStageCreateInfo shader_stage_info;
  shader_stage_info.stage = vk::ShaderStageFlagBits::eCompute;
  shader_stage_info.module = module;
  shader_stage_info.pName = "main";

  vk::ComputePipelineCreateInfo pipeline_info;
  pipeline_info.stage = shader_stage_info;
  pipeline_info.layout = pipeline_layout->vk();

  vk::Pipeline vk_pipeline = ESCHER_CHECKED_VK_RESULT(
      device.createComputePipeline(nullptr, pipeline_info));
  auto pipeline = fxl::MakeRefCounted<Pipeline>(
      device, vk_pipeline, pipeline_layout, PipelineSpec());

  device.destroyShaderModule(module);

  return pipeline;
}

// Used by ComputeShader constructor.
inline void InitWriteDescriptorSet(
    vk::WriteDescriptorSet& write,
    const std::vector<vk::DescriptorSetLayoutBinding>& layout_bindings,
    uint32_t binding_id) {
  write.dstArrayElement = 0;
  write.descriptorType = layout_bindings[binding_id].descriptorType;
  write.descriptorCount = 1;
  write.dstBinding = binding_id;
}

}  // namespace

ComputeShader::ComputeShader(
    EscherWeakPtr escher, const std::vector<vk::ImageLayout>& layouts,
    const std::vector<vk::DescriptorType>& buffer_types,
    size_t push_constants_size, const char* source_code)
    : device_(escher->vulkan_context().device),
      descriptor_set_layout_bindings_(
          CreateLayoutBindings(layouts, buffer_types)),
      descriptor_set_layout_create_info_(
          CreateDescriptorSetLayoutCreateInfo(descriptor_set_layout_bindings_)),
      push_constants_size_(static_cast<uint32_t>(push_constants_size)),
      pool_(escher, descriptor_set_layout_create_info_),
      pipeline_(CreatePipeline(device_, pool_.layout(), push_constants_size_,
                               source_code, escher->glsl_compiler())) {
  FXL_DCHECK(push_constants_size == push_constants_size_);  // detect overflow
  descriptor_image_info_.reserve(layouts.size());
  descriptor_buffer_info_.reserve(buffer_types.size());
  descriptor_set_writes_.reserve(layouts.size() + buffer_types.size());
  for (uint32_t index = 0; index < layouts.size(); ++index) {
    // The other fields will be filled out during each call to Dispatch().
    vk::DescriptorImageInfo image_info;
    image_info.imageLayout = layouts[index];
    descriptor_image_info_.push_back(image_info);

    vk::WriteDescriptorSet write;
    InitWriteDescriptorSet(write, descriptor_set_layout_bindings_, index);
    write.pImageInfo = &descriptor_image_info_[index];
    descriptor_set_writes_.push_back(write);
  }
  for (uint32_t i = 0; i < buffer_types.size(); ++i) {
    // The other fields will be filled out during each call to Dispatch().
    vk::DescriptorBufferInfo buffer_info;
    descriptor_buffer_info_.push_back(buffer_info);

    uint32_t binding = i + static_cast<uint32_t>(layouts.size());
    vk::WriteDescriptorSet write;
    InitWriteDescriptorSet(write, descriptor_set_layout_bindings_, binding);
    write.pBufferInfo = &descriptor_buffer_info_[i];
    descriptor_set_writes_.push_back(write);
  }
}

ComputeShader::~ComputeShader() {}

void ComputeShader::Dispatch(const std::vector<TexturePtr>& textures,
                             const std::vector<BufferPtr>& buffers,
                             CommandBuffer* command_buffer, uint32_t x,
                             uint32_t y, uint32_t z,
                             const void* push_constants) {
  std::vector<BufferRange> buffer_ranges;
  buffer_ranges.reserve(buffers.size());
  for (const auto& buffer : buffers) {
    buffer_ranges.push_back({0, buffer->size()});
  }
  DispatchWithRanges(textures, buffers, buffer_ranges, command_buffer, x, y, z,
                     push_constants);
}

void ComputeShader::DispatchWithRanges(
    const std::vector<TexturePtr>& textures,
    const std::vector<BufferPtr>& buffers,
    const std::vector<BufferRange>& buffer_ranges,
    CommandBuffer* command_buffer, uint32_t x, uint32_t y, uint32_t z,
    const void* push_constants) {
  // Push constants must be provided if and only if the pipeline is configured
  // to use them.
  FXL_DCHECK((push_constants_size_ == 0) == (push_constants == nullptr));
  FXL_DCHECK(buffers.size() == buffer_ranges.size());

  auto descriptor_set = pool_.Allocate(1, command_buffer)->get(0);
  for (uint32_t i = 0; i < textures.size(); ++i) {
    descriptor_set_writes_[i].dstSet = descriptor_set;
    descriptor_image_info_[i].imageView = textures[i]->vk_image_view();
    descriptor_image_info_[i].sampler = textures[i]->vk_sampler();
    command_buffer->KeepAlive(textures[i]);
  }
  for (uint32_t i = 0; i < buffers.size(); ++i) {
    uint32_t binding = i + static_cast<uint32_t>(textures.size());
    descriptor_set_writes_[binding].dstSet = descriptor_set;
    descriptor_buffer_info_[i].buffer = buffers[i]->vk();
    descriptor_buffer_info_[i].offset = buffer_ranges[i].offset;
    descriptor_buffer_info_[i].range = buffer_ranges[i].size;
    command_buffer->KeepAlive(buffers[i]);
  }
  device_.updateDescriptorSets(
      static_cast<uint32_t>(descriptor_set_writes_.size()),
      descriptor_set_writes_.data(), 0, nullptr);

  auto vk_command_buffer = command_buffer->vk();
  auto vk_pipeline_layout = pipeline_->vk_layout();

  if (push_constants) {
    vk_command_buffer.pushConstants(vk_pipeline_layout,
                                    vk::ShaderStageFlagBits::eCompute, 0,
                                    push_constants_size_, push_constants);
  }
  vk_command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute,
                                 pipeline_->vk());
  vk_command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                       vk_pipeline_layout, 0, 1,
                                       &descriptor_set, 0, nullptr);
  vk_command_buffer.dispatch(x, y, z);
}

}  // namespace impl
}  // namespace escher
