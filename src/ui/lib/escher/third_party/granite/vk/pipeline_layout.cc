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

#include "src/ui/lib/escher/third_party/granite/vk/pipeline_layout.h"

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/vk/impl/descriptor_set_allocator.h"

namespace escher {

const ResourceTypeInfo PipelineLayout::kTypeInfo("PipelineLayout",
                                                 ResourceType::kResource,
                                                 ResourceType::kPipelineLayout);

PipelineLayout::PipelineLayout(ResourceRecycler* resource_recycler,
                               const impl::PipelineLayoutSpec& spec)
    : Resource(resource_recycler), spec_(spec) {
  vk::DescriptorSetLayout set_layouts[VulkanLimits::kNumDescriptorSets] = {};
  unsigned num_set_layouts = 0;
  for (unsigned i = 0; i < VulkanLimits::kNumDescriptorSets; i++) {
    // TODO(ES-83): don't ask for an allocator if the set is masked?
    // Would be nice, but then we wouldn't have a layout available for the
    // skipped sets.
    descriptor_set_allocators_[i] =
        escher()->GetDescriptorSetAllocator(spec.descriptor_set_layouts[i]);
    set_layouts[i] = descriptor_set_allocators_[i]->vk_layout();
    if (spec.descriptor_set_mask & (1u << i)) {
      num_set_layouts = i + 1;
    }
  }

  unsigned num_ranges = 0;
  vk::PushConstantRange ranges[EnumCount<ShaderStage>()];

  for (auto& range : spec.push_constant_ranges) {
    if (range.size != 0) {
      bool unique = true;
      for (unsigned i = 0; i < num_ranges; i++) {
        // Try to merge equivalent ranges for multiple stages.
        // TODO(ES-83): what to do about overlapping ranges?  Should DCHECK?
        if (ranges[i].offset == range.offset && ranges[i].size == range.size) {
          unique = false;
          ranges[i].stageFlags |= range.stageFlags;
          break;
        }
      }

      if (unique)
        ranges[num_ranges++] = range;
    }
  }

  static_assert(sizeof(spec_.push_constant_ranges) == sizeof(ranges), "");
  FXL_DCHECK(num_ranges < EnumCount<ShaderStage>());
  memcpy(spec_.push_constant_ranges, ranges, num_ranges * sizeof(ranges[0]));
  spec_.num_push_constant_ranges = num_ranges;

  vk::PipelineLayoutCreateInfo info;
  if (num_set_layouts) {
    info.setLayoutCount = num_set_layouts;
    info.pSetLayouts = set_layouts;
  }

  if (num_ranges) {
    info.pushConstantRangeCount = num_ranges;
    info.pPushConstantRanges = ranges;
  }

  pipeline_layout_ =
      ESCHER_CHECKED_VK_RESULT(vk_device().createPipelineLayout(info, nullptr));
}

PipelineLayout::~PipelineLayout() {
  if (pipeline_layout_) {
    vk_device().destroyPipelineLayout(pipeline_layout_);
  }
}

}  // namespace escher
