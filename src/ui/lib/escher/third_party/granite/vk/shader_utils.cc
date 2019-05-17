/* Copyright (c) 2017 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

// Based on the following files from the Granite rendering engine:
// - vulkan/shader.cpp

#include "src/ui/lib/escher/third_party/granite/vk/shader_utils.h"

#include <vector>

#include "src/ui/lib/escher/third_party/granite/vk/pipeline_layout.h"
#include "src/ui/lib/escher/util/enum_cast.h"
#include "src/ui/lib/escher/util/hasher.h"
#include "src/ui/lib/escher/vk/shader_module.h"
#include "third_party/spirv-cross/spirv_cross.hpp"

namespace escher {
namespace impl {

void GenerateShaderModuleResourceLayoutFromSpirv(
    std::vector<uint32_t> spirv, ShaderStage stage,
    ShaderModuleResourceLayout* layout) {
  FXL_DCHECK(layout);

  // Clear layout before populating it.
  *layout = {};

  spirv_cross::Compiler compiler(std::move(spirv));
  vk::ShaderStageFlags stage_flags = ShaderStageToFlags(stage);

  auto resources = compiler.get_shader_resources();
  for (auto& image : resources.sampled_images) {
    auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
    auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
    auto& type = compiler.get_type(image.base_type_id);
    if (type.image.dim == spv::DimBuffer)
      layout->sets[set].sampled_buffer_mask |= 1u << binding;
    else
      layout->sets[set].sampled_image_mask |= 1u << binding;
    layout->sets[set].stages |= stage_flags;

    if (compiler.get_type(type.image.type).basetype ==
        spirv_cross::SPIRType::BaseType::Float)
      layout->sets[set].fp_mask |= 1u << binding;
  }

  for (auto& image : resources.subpass_inputs) {
    auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
    auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
    layout->sets[set].input_attachment_mask |= 1u << binding;
    layout->sets[set].stages |= stage_flags;

    auto& type = compiler.get_type(image.base_type_id);
    if (compiler.get_type(type.image.type).basetype ==
        spirv_cross::SPIRType::BaseType::Float)
      layout->sets[set].fp_mask |= 1u << binding;
  }

  for (auto& image : resources.storage_images) {
    auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
    auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
    layout->sets[set].storage_image_mask |= 1u << binding;
    layout->sets[set].stages |= stage_flags;

    auto& type = compiler.get_type(image.base_type_id);
    if (compiler.get_type(type.image.type).basetype ==
        spirv_cross::SPIRType::BaseType::Float)
      layout->sets[set].fp_mask |= 1u << binding;
  }

  for (auto& buffer : resources.uniform_buffers) {
    auto set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
    auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
    layout->sets[set].uniform_buffer_mask |= 1u << binding;
    layout->sets[set].stages |= stage_flags;
  }

  for (auto& buffer : resources.storage_buffers) {
    auto set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
    auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
    layout->sets[set].storage_buffer_mask |= 1u << binding;
    layout->sets[set].stages |= stage_flags;
  }

  // TODO(SCN-681): determine what is required to support other pipeline stages,
  // such as tessellation and geometry shaders.
  if (stage == ShaderStage::kVertex) {
    for (auto& attrib : resources.stage_inputs) {
      auto location =
          compiler.get_decoration(attrib.id, spv::DecorationLocation);
      layout->attribute_mask |= 1u << location;
    }
  } else if (stage == ShaderStage::kFragment) {
    for (auto& attrib : resources.stage_outputs) {
      auto location =
          compiler.get_decoration(attrib.id, spv::DecorationLocation);
      layout->render_target_mask |= 1u << location;
    }
  }

  if (!resources.push_constant_buffers.empty()) {
    // Need to declare the entire block.
    size_t size = compiler.get_declared_struct_size(compiler.get_type(
        resources.push_constant_buffers.front().base_type_id));
    layout->push_constant_offset = 0;
    layout->push_constant_range = size;
  }
}

PipelineLayoutSpec GeneratePipelineLayoutSpec(
    const std::array<ShaderModulePtr, EnumCount<ShaderStage>()>& shader_modules,
    const SamplerPtr& immutable_sampler) {
  uint32_t attribute_mask = 0;
  if (auto& vertex_module = shader_modules[EnumCast(ShaderStage::kVertex)]) {
    attribute_mask =
        vertex_module->shader_module_resource_layout().attribute_mask;
  }
  uint32_t render_target_mask = 0;
  if (auto& fragment_module =
          shader_modules[EnumCast(ShaderStage::kFragment)]) {
    render_target_mask =
        fragment_module->shader_module_resource_layout().render_target_mask;
  }

  std::array<DescriptorSetLayout, VulkanLimits::kNumDescriptorSets>
      descriptor_set_layouts;

  // Store the initial push constant ranges locally, then de-dup further down.
  std::array<vk::PushConstantRange, PipelineLayoutSpec::kMaxPushConstantRanges>
      raw_ranges = {};
  std::array<vk::PushConstantRange, PipelineLayoutSpec::kMaxPushConstantRanges>
      push_constant_ranges = {};
  for (uint32_t i = 0; i < EnumCount<ShaderStage>(); ++i) {
    auto& module = shader_modules[i];
    if (!module)
      continue;

    auto& module_layout = module->shader_module_resource_layout();

    for (uint32_t set = 0; set < VulkanLimits::kNumDescriptorSets; ++set) {
      impl::DescriptorSetLayout& pipe_dsl = descriptor_set_layouts[set];
      const impl::DescriptorSetLayout& mod_dsl = module_layout.sets[set];

      pipe_dsl.sampled_image_mask |= mod_dsl.sampled_image_mask;
      pipe_dsl.storage_image_mask |= mod_dsl.storage_image_mask;
      pipe_dsl.uniform_buffer_mask |= mod_dsl.uniform_buffer_mask;
      pipe_dsl.storage_buffer_mask |= mod_dsl.storage_buffer_mask;
      pipe_dsl.sampled_buffer_mask |= mod_dsl.sampled_buffer_mask;
      pipe_dsl.input_attachment_mask |= mod_dsl.input_attachment_mask;
      pipe_dsl.fp_mask |= mod_dsl.fp_mask;
      pipe_dsl.stages |= mod_dsl.stages;
    }

    raw_ranges[i].stageFlags = ShaderStageToFlags(ShaderStage(i));
    raw_ranges[i].offset = module_layout.push_constant_offset;
    raw_ranges[i].size = module_layout.push_constant_range;
  }

  unsigned num_ranges = 0;
  for (auto& range : raw_ranges) {
    if (range.size != 0) {
      bool unique = true;
      for (unsigned i = 0; i < num_ranges; i++) {
        // Try to merge equivalent ranges for multiple stages.
        // TODO(ES-83): what to do about overlapping ranges?  Should DCHECK?
        if (push_constant_ranges[i].offset == range.offset &&
            push_constant_ranges[i].size == range.size) {
          unique = false;
          push_constant_ranges[i].stageFlags |= range.stageFlags;
          break;
        }
      }

      if (unique)
        push_constant_ranges[num_ranges++] = range;
    }
  }

  uint32_t num_push_constant_ranges = num_ranges;

  return PipelineLayoutSpec(attribute_mask, render_target_mask,
                            descriptor_set_layouts, push_constant_ranges,
                            num_push_constant_ranges, immutable_sampler);
}

}  // namespace impl
}  // namespace escher
