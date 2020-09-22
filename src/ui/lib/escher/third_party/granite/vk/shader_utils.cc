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

void GenerateShaderModuleResourceLayoutFromSpirv(std::vector<uint32_t> spirv, ShaderStage stage,
                                                 ShaderModuleResourceLayout* layout) {
  FX_DCHECK(layout);

  // Clear layout before populating it.
  *layout = {};

  spirv_cross::Compiler compiler(std::move(spirv));
  vk::ShaderStageFlags stage_flags = ShaderStageToFlags(stage);

  auto resources = compiler.get_shader_resources();
  for (auto& image : resources.sampled_images) {
    uint32_t set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
    uint32_t binding = compiler.get_decoration(image.id, spv::DecorationBinding);
    const spirv_cross::SPIRType& type = compiler.get_type(image.base_type_id);
    if (type.image.dim == spv::DimBuffer)
      layout->sets[set].sampled_buffer_mask |= 1u << binding;
    else
      layout->sets[set].sampled_image_mask |= 1u << binding;
    layout->sets[set].stages |= stage_flags;

    if (compiler.get_type(type.image.type).basetype == spirv_cross::SPIRType::BaseType::Float)
      layout->sets[set].fp_mask |= 1u << binding;
  }

  for (auto& image : resources.subpass_inputs) {
    uint32_t set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
    uint32_t binding = compiler.get_decoration(image.id, spv::DecorationBinding);
    layout->sets[set].input_attachment_mask |= 1u << binding;
    layout->sets[set].stages |= stage_flags;

    auto& type = compiler.get_type(image.base_type_id);
    if (compiler.get_type(type.image.type).basetype == spirv_cross::SPIRType::BaseType::Float)
      layout->sets[set].fp_mask |= 1u << binding;
  }

  for (auto& image : resources.storage_images) {
    uint32_t set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
    uint32_t binding = compiler.get_decoration(image.id, spv::DecorationBinding);
    layout->sets[set].storage_image_mask |= 1u << binding;
    layout->sets[set].stages |= stage_flags;

    auto& type = compiler.get_type(image.base_type_id);
    if (compiler.get_type(type.image.type).basetype == spirv_cross::SPIRType::BaseType::Float)
      layout->sets[set].fp_mask |= 1u << binding;
  }

  for (auto& buffer : resources.uniform_buffers) {
    uint32_t set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
    uint32_t binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
    layout->sets[set].uniform_buffer_mask |= 1u << binding;
    layout->sets[set].stages |= stage_flags;
  }

  for (auto& buffer : resources.storage_buffers) {
    uint32_t set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
    uint32_t binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
    layout->sets[set].storage_buffer_mask |= 1u << binding;
    layout->sets[set].stages |= stage_flags;
  }

  // TODO(fxbug.dev/7321): determine what is required to support other pipeline stages,
  // such as tessellation and geometry shaders.
  if (stage == ShaderStage::kVertex) {
    for (auto& attrib : resources.stage_inputs) {
      auto location = compiler.get_decoration(attrib.id, spv::DecorationLocation);
      layout->attribute_mask |= 1u << location;
    }
  } else if (stage == ShaderStage::kFragment) {
    for (auto& attrib : resources.stage_outputs) {
      auto location = compiler.get_decoration(attrib.id, spv::DecorationLocation);
      layout->render_target_mask |= 1u << location;
    }
  }

  if (!resources.push_constant_buffers.empty()) {
    // In the general case, it is possible for a single shader module to contain
    // multiple push constant blocks, however since it is unlikely that any shaders
    // that we write will have multiple blocks per module, we assume that there is
    // at most one to simplify the following implementation. It is possible that in
    // the future we may wish to change this.
    FX_DCHECK(resources.push_constant_buffers.size() == 1);

    // Need to declare the entire block.
    // Get the type for the current push constant range (e.g. vertex, fragment, etc).
    const spirv_cross::SPIRType& type =
        compiler.get_type(resources.push_constant_buffers.front().base_type_id);

    // The offset for the current push constant range should be equal to the offset of the
    // first declared member of that range.
    layout->push_constant_offset = compiler.type_struct_member_offset(type, /*first member*/ 0);

    // The total size of the range can be calculated by adding together the offset + size of the
    // *last* member of the struct, and subtracting out the offset of the *first* member of the
    // struct. It would also be possible to simply loop over each of the member types and sum their
    // sizes, but the first approach gives us a constant time solution (and allows us to avoid
    // having to reason about padding).
    uint32_t last = uint32_t(type.member_types.size() - 1);
    uint32_t last_offset = compiler.type_struct_member_offset(type, last);
    layout->push_constant_range = last_offset +
                                  compiler.get_declared_struct_member_size(type, last) -
                                  layout->push_constant_offset;
  }
}

// This function uses a generic interval merging algorithm. We start by sorting
// the ranges by their offsets, and then traverse the sorted vector, combining
// ranges that overlap as we go.
std::vector<vk::PushConstantRange> ConsolidatePushConstantRanges(
    const std::vector<vk::PushConstantRange>& input_ranges) {
  // Do nothing if the ranges are empty or if there is only one range.
  if (input_ranges.size() <= 1) {
    return input_ranges;
  }

  // Copy the input vector and sort it based off the starting offsets.
  auto ranges = input_ranges;
  std::sort(ranges.begin(), ranges.end(),
            [](vk::PushConstantRange& a, vk::PushConstantRange& b) { return a.offset < b.offset; });

  // Get the starting data from the first range in the vector.
  auto& first = ranges[0];
  uint32_t start = first.offset;
  uint32_t end = start + first.size;
  auto flags = first.stageFlags;

  // Iterate over all of the subsequent ranges. If the current range's offset is
  // less than ending point of the previous range, we combine the two, and update
  // the stage flags. If the current offset starts after the end of the previous
  // range, then we have a new range that we can push back into our result vector.
  std::vector<vk::PushConstantRange> result;
  for (uint32_t i = 1; i < ranges.size(); i++) {
    auto& current = ranges[i];
    // This is specifically a less-than and not less-than-or-equal-to so that
    // adjacent but not overlapping ranges are not merged.
    if (current.offset < end) {
      end = std::max(current.offset + current.size, end);
      flags |= current.stageFlags;
    } else {
      result.push_back(vk::PushConstantRange(flags, start, end - start));
      start = current.offset;
      end = start + current.size;
      flags = current.stageFlags;
    }
  }

  // Perform one more push back for the very last range, then return the result.
  result.push_back(vk::PushConstantRange(flags, start, end - start));
  return result;
}

PipelineLayoutSpec GeneratePipelineLayoutSpec(
    const std::array<ShaderModulePtr, EnumCount<ShaderStage>()>& shader_modules,
    const SamplerPtr& immutable_sampler) {
  uint32_t attribute_mask = 0;
  if (auto& vertex_module = shader_modules[EnumCast(ShaderStage::kVertex)]) {
    attribute_mask = vertex_module->shader_module_resource_layout().attribute_mask;
  }
  uint32_t render_target_mask = 0;
  if (auto& fragment_module = shader_modules[EnumCast(ShaderStage::kFragment)]) {
    render_target_mask = fragment_module->shader_module_resource_layout().render_target_mask;
  }

  std::array<DescriptorSetLayout, VulkanLimits::kNumDescriptorSets> descriptor_set_layouts;

  // Store the initial push constant ranges locally, then de-dup further down.
  std::vector<vk::PushConstantRange> raw_ranges;

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
    vk::PushConstantRange range;
    range.stageFlags = ShaderStageToFlags(ShaderStage(i));
    range.offset = module_layout.push_constant_offset;
    range.size = module_layout.push_constant_range;
    raw_ranges.push_back(range);
  }

  auto push_constant_ranges = ConsolidatePushConstantRanges(raw_ranges);
  uint32_t num_push_constant_ranges = push_constant_ranges.size();

  return PipelineLayoutSpec(attribute_mask, render_target_mask, descriptor_set_layouts,
                            push_constant_ranges, num_push_constant_ranges, immutable_sampler);
}

}  // namespace impl
}  // namespace escher
