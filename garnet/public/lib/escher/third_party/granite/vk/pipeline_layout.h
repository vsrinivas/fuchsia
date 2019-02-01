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
// - vulkan/shader.hpp

#ifndef LIB_ESCHER_THIRD_PARTY_GRANITE_VK_PIPELINE_LAYOUT_H_
#define LIB_ESCHER_THIRD_PARTY_GRANITE_VK_PIPELINE_LAYOUT_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/resources/resource.h"
#include "lib/escher/third_party/granite/vk/descriptor_set_layout.h"
#include "lib/escher/util/debug_print.h"
#include "lib/escher/util/enum_count.h"
#include "lib/escher/util/hash.h"
#include "lib/escher/vk/shader_stage.h"
#include "lib/escher/vk/vulkan_limits.h"

namespace escher {
namespace impl {

// Aggregate the ShaderModuleResourceLayouts of all ShaderModules that are used
// to create a pipeline.
struct PipelineLayoutSpec {
  uint32_t attribute_mask = 0;
  // TODO(ES-83): document.
  uint32_t render_target_mask = 0;
  DescriptorSetLayout descriptor_set_layouts[VulkanLimits::kNumDescriptorSets] =
      {};
  vk::PushConstantRange push_constant_ranges[EnumCount<ShaderStage>()] = {};
  uint32_t num_push_constant_ranges = 0;
  uint32_t descriptor_set_mask = 0;

  // Allows quick comparison to decide whether the push constant ranges have
  // changed.  If so, all descriptor sets are invalidated.
  // TODO(ES-83): I remember reading why this is necessary... we should
  // make note of the section of the Vulkan spec that requires this.
  Hash push_constant_layout_hash = {0};

  bool operator==(const PipelineLayoutSpec& other) const;
};

// TODO(ES-83): extend downward to enclose PipelineLayout.  Cannot do this yet
// because there is already a PipelineLayout in impl/vk.
}  // namespace impl

// A PipelineLayout encapsulates a VkPipelineLayout object, as well as an array
// of DescriptorSetAllocators that are configured to allocate descriptor sets
// that match the sets required, at each index, by pipelines with this layout.
//
// TODO(ES-83): does this need to be a Resource?  If these are always
// reffed by pipelines that use them, then it should suffice to keep those
// pipelines alive, right?
class PipelineLayout : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  PipelineLayout(ResourceRecycler* resource_recycler,
                 const impl::PipelineLayoutSpec& spec);
  ~PipelineLayout();

  vk::PipelineLayout vk() const { return pipeline_layout_; }

  const impl::PipelineLayoutSpec& spec() const { return spec_; }

  impl::DescriptorSetAllocator* GetDescriptorSetAllocator(
      unsigned set_index) const {
    FXL_DCHECK(set_index < VulkanLimits::kNumDescriptorSets);
    return descriptor_set_allocators_[set_index];
  }

 private:
  vk::PipelineLayout pipeline_layout_;
  impl::PipelineLayoutSpec spec_;
  impl::DescriptorSetAllocator*
      descriptor_set_allocators_[VulkanLimits::kNumDescriptorSets] = {};
};

using PipelineLayoutPtr = fxl::RefPtr<PipelineLayout>;

// Inline function definitions.

inline bool impl::PipelineLayoutSpec::operator==(
    const impl::PipelineLayoutSpec& other) const {
  return attribute_mask == other.attribute_mask &&
         render_target_mask == other.render_target_mask &&
         descriptor_set_mask == other.descriptor_set_mask &&
         push_constant_layout_hash == other.push_constant_layout_hash &&
         num_push_constant_ranges == other.num_push_constant_ranges &&
         0 == memcmp(descriptor_set_layouts, other.descriptor_set_layouts,
                     sizeof(descriptor_set_layouts)) &&
         0 == memcmp(push_constant_ranges, other.push_constant_ranges,
                     sizeof(push_constant_ranges));
}

ESCHER_DEBUG_PRINTABLE(impl::PipelineLayoutSpec);

}  // namespace escher

#endif  // LIB_ESCHER_THIRD_PARTY_GRANITE_VK_PIPELINE_LAYOUT_H_
