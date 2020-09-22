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

#ifndef SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_PIPELINE_LAYOUT_H_
#define SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_PIPELINE_LAYOUT_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/resources/resource.h"
#include "src/ui/lib/escher/third_party/granite/vk/descriptor_set_layout.h"
#include "src/ui/lib/escher/util/bit_ops.h"
#include "src/ui/lib/escher/util/debug_print.h"
#include "src/ui/lib/escher/util/enum_count.h"
#include "src/ui/lib/escher/util/hashable.h"
#include "src/ui/lib/escher/util/hasher.h"
#include "src/ui/lib/escher/vk/impl/descriptor_set_allocator.h"
#include "src/ui/lib/escher/vk/sampler.h"
#include "src/ui/lib/escher/vk/shader_stage.h"
#include "src/ui/lib/escher/vk/vulkan_limits.h"

#include <vulkan/vulkan.hpp>

namespace escher {
namespace impl {

// Aggregate the ShaderModuleResourceLayouts of all ShaderModules that are
// used to create a pipeline.
struct PipelineLayoutSpec : public Hashable {
 public:
  static constexpr size_t kMaxPushConstantRanges = EnumCount<ShaderStage>();

  PipelineLayoutSpec(
      uint32_t attribute_mask, uint32_t render_target_mask,
      const std::array<DescriptorSetLayout, VulkanLimits::kNumDescriptorSets>& layouts,
      const std::vector<vk::PushConstantRange>& ranges, uint32_t num_ranges,
      SamplerPtr immutable_sampler)
      : immutable_sampler_(immutable_sampler),
        attribute_mask_(attribute_mask),
        render_target_mask_(render_target_mask),
        descriptor_set_layouts_(layouts),
        num_push_constant_ranges_(num_ranges),
        push_constant_ranges_(ranges) {
    FX_DCHECK(num_ranges < kMaxPushConstantRanges);
    for (uint32_t i = 0; i < VulkanLimits::kNumDescriptorSets; ++i) {
      if (descriptor_set_layouts_[i].stages) {
        descriptor_set_mask_ |= 1u << i;
      }
    }

    // Compute a hash to quickly decide whether all descriptor sets must be
    // invalidated.
    Hasher h;
    for (uint32_t i = 0; i < num_ranges; ++i) {
      h.struc(push_constant_ranges_[i]);
    }
    push_constant_layout_hash_ = h.value();
  }
  virtual ~PipelineLayoutSpec() = default;

  const SamplerPtr& immutable_sampler() const { return immutable_sampler_; }

  uint32_t attribute_mask() const { return attribute_mask_; }
  uint32_t render_target_mask() const { return render_target_mask_; }

  uint32_t descriptor_set_mask() const { return descriptor_set_mask_; }
  const DescriptorSetLayout& descriptor_set_layouts(uint32_t index) const {
    return descriptor_set_layouts_[index];
  }
  uint32_t num_push_constant_ranges() const { return num_push_constant_ranges_; }
  const std::vector<vk::PushConstantRange>& push_constant_ranges() const {
    return push_constant_ranges_;
  }

  Hash push_constant_layout_hash() const { return push_constant_layout_hash_; }

  bool operator==(const PipelineLayoutSpec& other) const;

 private:
  // |Hashable|
  Hash GenerateHash() const override;

  SamplerPtr immutable_sampler_;
  uint32_t attribute_mask_ = 0;
  // TODO(fxbug.dev/7174): document.
  uint32_t render_target_mask_ = 0;
  uint32_t descriptor_set_mask_ = 0;
  std::array<DescriptorSetLayout, VulkanLimits::kNumDescriptorSets> descriptor_set_layouts_ = {};
  uint32_t num_push_constant_ranges_ = 0;
  std::vector<vk::PushConstantRange> push_constant_ranges_ = {};

  // Allows quick comparison to decide whether the push constant ranges have
  // changed.  If so, all descriptor sets are invalidated.
  // TODO(fxbug.dev/7174): I remember reading why this is necessary... we should
  // make note of the section of the Vulkan spec that requires this.
  Hash push_constant_layout_hash_ = {0};
};

// TODO(fxbug.dev/7174): extend downward to enclose PipelineLayout.  Cannot do this yet
// because there is already a PipelineLayout in impl/vk.
}  // namespace impl

// A PipelineLayout encapsulates a VkPipelineLayout object, as well as an
// array of DescriptorSetAllocators that are configured to allocate descriptor
// sets that match the sets required, at each index, by pipelines with this
// layout.
//
// TODO(fxbug.dev/7174): does this need to be a Resource?  If these are always
// reffed by pipelines that use them, then it should suffice to keep those
// pipelines alive, right?
class PipelineLayout : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  PipelineLayout(ResourceRecycler* resource_recycler, const impl::PipelineLayoutSpec& spec);
  ~PipelineLayout();

  vk::PipelineLayout vk() const { return pipeline_layout_; }

  const impl::PipelineLayoutSpec& spec() const { return spec_; }

  impl::DescriptorSetAllocator* GetDescriptorSetAllocator(unsigned set_index) const {
    FX_DCHECK(set_index < VulkanLimits::kNumDescriptorSets);
    return descriptor_set_allocators_[set_index].get();
  }

 private:
  vk::PipelineLayout pipeline_layout_;
  // This PipelineLayoutSpec will be used for hashes and equality tests, so it
  // should match the construction parameter and not be mutated.
  const impl::PipelineLayoutSpec spec_;
  impl::DescriptorSetAllocatorPtr descriptor_set_allocators_[VulkanLimits::kNumDescriptorSets] = {};
};

using PipelineLayoutPtr = fxl::RefPtr<PipelineLayout>;

// Inline function definitions.

inline bool impl::PipelineLayoutSpec::operator==(const impl::PipelineLayoutSpec& other) const {
  return immutable_sampler_ == other.immutable_sampler_ &&
         attribute_mask_ == other.attribute_mask_ &&
         render_target_mask_ == other.render_target_mask_ &&
         descriptor_set_mask_ == other.descriptor_set_mask_ &&
         descriptor_set_layouts_ == other.descriptor_set_layouts_ &&
         num_push_constant_ranges_ == other.num_push_constant_ranges_ &&
         push_constant_ranges_ == other.push_constant_ranges_ &&
         push_constant_layout_hash_ == other.push_constant_layout_hash_;
}

inline Hash impl::PipelineLayoutSpec::GenerateHash() const {
  Hasher h;

  h.struc(immutable_sampler_);
  h.u32(attribute_mask_);
  h.u32(render_target_mask_);

  h.u32(descriptor_set_mask_);
  ForEachBitIndex(descriptor_set_mask_,
                  [&](uint32_t index) { h.struc(descriptor_set_layouts_[index]); });

  // Instead of hashing the push constant ranges again, just hash the hash of
  // the push constant ranges that was already computed in the constructor.
  h.u64(push_constant_layout_hash_.val);

  return h.value();
}

ESCHER_DEBUG_PRINTABLE(impl::PipelineLayoutSpec);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_PIPELINE_LAYOUT_H_
