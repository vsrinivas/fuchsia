// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/impl/descriptor_set_allocator_cache.h"

#include "src/ui/lib/escher/util/hasher.h"
#include "src/ui/lib/escher/util/trace_macros.h"

namespace escher {
namespace impl {

DescriptorSetAllocatorCache::DescriptorSetAllocatorCache(vk::Device device) : device_(device) {}

DescriptorSetAllocatorPtr DescriptorSetAllocatorCache::ObtainDescriptorSetAllocator(
    const DescriptorSetLayout& layout, const SamplerPtr& immutable_sampler) {
  TRACE_DURATION("gfx", "escher::impl::DescriptorSetAllocatorCache::ObtainDescriptorSetAllocator");
  static_assert(sizeof(DescriptorSetLayout) == 32, "hash code below must be updated");
  Hasher h;
  if (immutable_sampler)
    h.struc(immutable_sampler->vk());
  h.u32(layout.sampled_image_mask);
  h.u32(layout.storage_image_mask);
  h.u32(layout.uniform_buffer_mask);
  h.u32(layout.storage_buffer_mask);
  h.u32(layout.sampled_buffer_mask);
  h.u32(layout.input_attachment_mask);
  h.u32(layout.fp_mask);
  h.u32(static_cast<uint32_t>(layout.stages));
  Hash hash = h.value();

  auto it = descriptor_set_allocators_.find(hash);
  if (it != descriptor_set_allocators_.end() && !it->second.expired()) {
    return it->second.lock();
  }

  DescriptorSetAllocatorPtr allocator;
  {
    TRACE_DURATION(
        "gfx", "escher::impl::DescriptorSetAllocatorCache::ObtainDescriptorSetAllocator[creation]");
    allocator = std::make_shared<DescriptorSetAllocator>(device_, layout, immutable_sampler);
  }
  descriptor_set_allocators_[hash] = allocator;
  return allocator;
}

void DescriptorSetAllocatorCache::BeginFrame() {
  auto it = descriptor_set_allocators_.begin();
  while (it != descriptor_set_allocators_.end()) {
    if (it->second.expired()) {
      it = descriptor_set_allocators_.erase(it);
    } else {
      it->second.lock()->BeginFrame();
      ++it;
    }
  }
}

}  // namespace impl
}  // namespace escher
